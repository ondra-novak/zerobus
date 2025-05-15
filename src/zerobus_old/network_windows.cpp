#include "network_windows.h"
#include <MSWSock.h>
#include <ws2tcpip.h>
#include <assert.h>

#pragma comment(lib, "Ws2_32.lib")


namespace zerobus {

static thread_local unsigned int callback_call_counter = 0;

class MsWSock {
public:
    MsWSock() {
        int rc = WSAStartup(MAKEWORD(2,2), &wsadata);
        if (rc != 0) {
            throw std::runtime_error("Failed to initialize winsock: error=" + std::to_string(rc));
        }

        SOCKET sock;
        DWORD dwBytes;
        

        /* Dummy socket needed for WSAIoctl */
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET)
                throw std::runtime_error("MSWSOCK: failed to create dummy socket");

        {
            GUID guid = WSAID_CONNECTEX;
            rc = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &guid, sizeof(guid),
                        &ConnectEx, sizeof(ConnectEx),
                        &dwBytes, NULL, NULL);
            if (rc != 0)
                throw std::runtime_error("MSWSOCK function ConnectEx is unavailable");
        }

        {
            GUID guid = WSAID_ACCEPTEX;
            rc = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &guid, sizeof(guid),
                        &AcceptEx, sizeof(AcceptEx),
                        &dwBytes, NULL, NULL);
            if (rc != 0)
                throw std::runtime_error("MSWSOCK function AcceptEx is unavailable");
        }

        {
            GUID guid = WSAID_GETACCEPTEXSOCKADDRS;
            rc = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &guid, sizeof(guid),
                        &GetAcceptExSockaddrs, sizeof(GetAcceptExSockaddrs),
                        &dwBytes, NULL, NULL);
            if (rc != 0)
                throw std::runtime_error("MSWSOCK function GetAcceptExSockAddrs is unavailable");
        }

        closesocket(sock);

    }    
    ~MsWSock() {
        WSACleanup();
    }
    LPFN_CONNECTEX ConnectEx;
    LPFN_ACCEPTEX AcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs;

    WSADATA wsadata;
};

static MsWSock mswsock;

static void default_log_function(std::string_view action, std::source_location loc) {
    std::ostringstream ostr;
    auto tp = std::time(nullptr);
    std::tm ts;
    gmtime_s(&ts, &tp);
    std::string msg;
    try {
        throw;
    } catch (const std::exception &e) {
        msg = e.what();
    } catch (...) {
        msg = "Unknown error";
    }
    ostr << std::put_time(&ts, "%F %T") << " " << loc.file_name() << "(" << loc.line() << ") :" << msg << " [" << action << "]";
    auto w = ostr.view();
    int needsz = MultiByteToWideChar(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),0,0);
    std::wstring wmsg;
    wmsg.resize(needsz);
    MultiByteToWideChar(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), wmsg.data(), needsz);
    OutputDebugStringW(wmsg.c_str());
}



std::shared_ptr<INetContext> make_network_context(int iothreads) {
    return make_network_context(default_log_function, iothreads);
}

std::shared_ptr<INetContext> make_network_context(ErrorCallback ecb, int iothreads) {
    auto p = std::make_shared<NetThreadedContext>(std::move(ecb), iothreads);
    p->start();
    return p;
}

NetThreadedContext::NetThreadedContext(ErrorCallback ecb, int threads)
    :NetContextWin(std::move(ecb))
    ,_threads(threads)
{
}

NetThreadedContext::~NetThreadedContext() {
    for (auto &t: _threads) {
        if (t.joinable() && t.get_id() == std::this_thread::get_id()) {
            t.request_stop();
            t.detach();
        }
    }
}

void NetThreadedContext::start() {
    for (auto &t: _threads) {
        t = run_thread();
    }
}

std::string sockaddr_to_string(const sockaddr* addr) {
    char host[NI_MAXHOST] = {0};
    char port[NI_MAXSERV] = {0};

    if (!addr) {
        return "unknown";
    }

    switch (addr->sa_family) {
        case AF_INET: {  // IPv4
            const sockaddr_in* ipv4_addr = reinterpret_cast<const sockaddr_in*>(addr);
            inet_ntop(AF_INET, &(ipv4_addr->sin_addr), host, sizeof(host));
            snprintf(port, sizeof(port), "%d", ntohs(ipv4_addr->sin_port));
            return std::string(host) + ":" + port;
        }

        case AF_INET6: {  // IPv6
            const sockaddr_in6* ipv6_addr = reinterpret_cast<const sockaddr_in6*>(addr);
            inet_ntop(AF_INET6, &(ipv6_addr->sin6_addr), host, sizeof(host));
            snprintf(port, sizeof(port), "%d", ntohs(ipv6_addr->sin6_port));
            return "[" + std::string(host) + "]:" + port;
        }

        default:
            return "unknown";
    }
}


ConnHandle NetContextWin::create_server(std::string address_port) {
    size_t port_pos = address_port.rfind(':');
    if (port_pos == std::string::npos) {
        throw std::invalid_argument("Invalid address format (missing port)");
    }

    std::string host = address_port.substr(0, port_pos);
    std::string port = address_port.substr(port_pos + 1);

    if (host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }

    if (host == "*") {
        host = "";
    }

    if (port == "*") {
        port = "0";
    }

    struct addrinfo hints ={};
    struct addrinfo* res;
    int af = AF_INET;

    hints.ai_family = AF_UNSPEC;       // IPv4 nebo IPv6
    hints.ai_socktype = SOCK_STREAM;   // TCP
    hints.ai_flags = AI_PASSIVE;       // Použít pro bind()

    int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (status != 0) {
        throw std::invalid_argument("Invalid address or port: " + std::string(gai_strerror(status)));
    }

    SOCKET listen_fd = static_cast<SOCKET>(-1);

    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        af = p->ai_family;
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd == -1) {
            continue;
        }

        if (bind(listen_fd, p->ai_addr, static_cast<int>(p->ai_addrlen)) == -1) {
            closesocket(listen_fd);
            listen_fd = static_cast<SOCKET>(-1);
            continue;
        }

        break;
    }


    freeaddrinfo(res);

    if (listen_fd == static_cast<SOCKET>(-1)) {
        throw std::system_error(errno, std::generic_category(), "Failed to bind to address");
    }

    if (listen(listen_fd, SOMAXCONN) == -1) {
        closesocket(listen_fd);
        throw std::system_error(errno, std::generic_category(), "Failed to listen on socket");
    }

    std::lock_guard _(_mx);
    SocketInfo *nfo = alloc_socket_lk();
    nfo->_socket = listen_fd;
    nfo->_af = af;
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(nfo->_socket), _completion_port, nfo->_ident+key_offset,0)    ;
    return nfo->_ident;

}


NetContextWin::SocketInfo *NetContextWin::alloc_socket_lk() {
     while (_first_free_socket_ident >= _sockets.size()) {
        _sockets.push_back(std::make_unique<SocketInfo>());        
        _sockets.back()->_ident = static_cast<ConnHandle>(_sockets.size());
    }
    SocketInfo *nfo = _sockets[_first_free_socket_ident].get();
    std::swap(nfo->_ident,_first_free_socket_ident);
    return nfo;
}


NetContextWin::NetContextWin(ErrorCallback ecb)
    :_ecb(std::move(ecb))
    ,_tmset(TimeoutSet::allocator_type(&_pool))
 {
    _completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);    
}

NetContextWin::NetContextWin(): NetContextWin(default_log_function) {}

NetContextWin::~NetContextWin() {
    CloseHandle(_completion_port);
}

void setSocketNonBlocking(SOCKET sock) {
    u_long mode = 1; 
    std::ignore = ioctlsocket(sock, FIONBIO, &mode);
}

SOCKET NetContextWin::connect_peer(std::string address_port, DWORD key, OVERLAPPED *ovr) {
    size_t port_pos = address_port.rfind(':');
    if (port_pos == std::string::npos) {
        throw std::invalid_argument("Invalid address format (missing port)");
    }

    std::string host = address_port.substr(0, port_pos);
    std::string port = address_port.substr(port_pos + 1);

    struct addrinfo hints = {};
    struct addrinfo* res;

    hints.ai_family = AF_UNSPEC;       // IPv4 nebo IPv6
    hints.ai_socktype = SOCK_STREAM;   // TCP
    hints.ai_flags = AI_NUMERICSERV;   // Očekáváme numerický port

    if (host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }

    int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (status != 0) {
        throw std::invalid_argument("Invalid address or port: " + std::string(gai_strerror(status)));
    }


    SOCKET sockfd =  INVALID_SOCKET;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol);
        if (sockfd ==  INVALID_SOCKET) {
            continue;
        }
        {
            struct sockaddr_storage addr;
            ZeroMemory(&addr, sizeof(addr));
            addr.ss_family = static_cast<ADDRESS_FAMILY>(p->ai_family);
            bind(sockfd, (SOCKADDR*) &addr, static_cast<int>(p->ai_addrlen));
        }

        setSocketNonBlocking(sockfd);
        CreateIoCompletionPort(reinterpret_cast<HANDLE>(sockfd), _completion_port, key, 0);
        ZeroMemory(ovr, sizeof(OVERLAPPED));
        if (!mswsock.ConnectEx(sockfd,p->ai_addr, static_cast<int>(p->ai_addrlen), NULL, 0, NULL, ovr)) {
            auto e = WSAGetLastError();
            if (e != ERROR_IO_PENDING) {
                closesocket(sockfd);
                sockfd =  INVALID_SOCKET;
                continue;
            }
        }
        break;
    }

    freeaddrinfo(res);

    if (sockfd ==  INVALID_SOCKET) {
        throw std::system_error(errno, std::generic_category(), "Failed to connect");
    }
    return sockfd;

}


void NetContextWin::free_socket_lk(ConnHandle id) {
    SocketInfo *nfo = _sockets[id].get();
    assert(nfo->_cb_call_cntr == 0);
    std::destroy_at(nfo);
    std::construct_at(nfo);
    nfo->_ident = _first_free_socket_ident;
    _first_free_socket_ident = id;
}


NetContextWin::SocketInfo *NetContextWin::socket_by_ident(ConnHandle id) {
    if (id >= _sockets.size()) return nullptr;
    auto r = _sockets[id].get();
    return r->_ident == id?r:nullptr;
}


ConnHandle NetContextWin::connect(std::string address_port)  {
    std::lock_guard _(_mx);
    auto ctx = alloc_socket_lk();
    try {
        SOCKET s =  connect_peer(std::move(address_port),ctx->_ident+key_offset,&ctx->_send_ovr);
        ctx->_socket = s;
        ctx->_connecting = true;
        return ctx->_ident;
    } catch (...) {
        free_socket_lk(ctx->_ident);
        throw;
    }
}

 void NetContextWin::reconnect(ConnHandle ident, std::string address_port) {
    ConnHandle oldh;
        {
        std::lock_guard _(_mx);
        auto nctx = alloc_socket_lk();
        try {
            SOCKET s = connect_peer(std::move(address_port),ident+key_offset,&nctx->_send_ovr);
            nctx->_socket = s;
            nctx->_connecting = true;
        } catch (...) {
            free_socket_lk(nctx->_ident);
            throw;
        }
        oldh = nctx->_ident;
        auto octx = socket_by_ident(ident);
        if (octx) {
            std::swap(_sockets[oldh], _sockets[ident]);
            nctx->_connecting = true;
            nctx->_ident = ident;
            octx->_ident = oldh;
            nctx->_timeout_cb = octx->_timeout_cb;
            nctx->_tmtp = octx->_tmtp;
            nctx->_cb_call_cntr = octx->_cb_call_cntr;
            octx->_cb_call_cntr = 0;
        }
    }
    destroy(oldh);
 }



void NetContextWin::run_worker(std::stop_token tkn)  {
    std::unique_lock lk(_mx);
    std::stop_callback __(tkn, [&]{
        PostQueuedCompletionStatus(_completion_port,0,key_exit,NULL);
    });

    std::vector<SimpleAction> actions;

    while (!tkn.stop_requested()) {
        DWORD timeout = INFINITE;
        DWORD transfered;
        ULONG_PTR key;
        LPOVERLAPPED ovr = NULL;

        bool timeout_thread = _need_timeout_thread;
        _need_timeout_thread = false;
        if (timeout_thread) {
            timeout = get_completion_timeout_lk();
            timeout_thread= true;
        }
        lk.unlock();
        BOOL res;
        res = GetQueuedCompletionStatus(_completion_port,&transfered, &key, &ovr, timeout);    
        lk.lock();
        _need_timeout_thread = true;
        if (!res && ovr == NULL) { //timeout
            auto now = std::chrono::system_clock::now();
            while (!_tmset.empty()) {
                auto iter = _tmset.begin();
                if (iter->first > now) break;
                ConnHandle id = iter->second;
                _tmset.erase(iter);
                SocketInfo *nfo = socket_by_ident(id);
                if (nfo && nfo->_timeout_cb) {
                    auto cb = std::exchange(nfo->_timeout_cb, nullptr);
                    invoke_cb_lk(lk, id, [&]{cb->on_timeout();});
                }
            }
        } else {
            if (key >= key_offset) {
                auto err = GetLastError();
                if (res) err = 0;
                process_event_lk(lk, static_cast<ConnHandle>(key-key_offset), transfered, ovr, err);
            }
        }
        std::swap(actions, _actions);
        while (!actions.empty()) {
            lk.unlock();
            for (auto &x: actions) {
                x();
                if (tkn.stop_requested()) return;
            }
            actions.clear();
            lk.lock();
            std::swap(actions, _actions);
        }
    }

}

DWORD NetContextWin::get_completion_timeout_lk()
{
    auto tp = get_completion_timeout_tp_lk();
    if (tp == std::chrono::system_clock::time_point::max()) return INFINITE;
    auto now = std::chrono::system_clock::now();
    if (tp < now) return 0;
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp - now).count();
    if (diff > static_cast<decltype(diff)>(std::numeric_limits<DWORD>::max())) {
        return INFINITE-1;
    } 
    return static_cast<DWORD>(diff);
}

std::chrono::system_clock::time_point NetContextWin::get_completion_timeout_tp_lk()
{
    if (!_tmset.empty()) {
        return _tmset.begin()->first;
    }
    return std::chrono::system_clock::time_point::max();
}

void NetContextWin::process_event_lk(std::unique_lock<std::mutex> &lk, ConnHandle h,  DWORD transfered, OVERLAPPED *ovr, DWORD error) {
    auto ctx = socket_by_ident(h);
    if (!ctx) return;    
    if (ovr == &ctx->_send_ovr) {   //POLLOUT
        if (ctx->_destroy_on_cancel_write) {
            ctx->_destroy_on_cancel_write = false;
            if (!ctx->_destroy_on_cancel_read) {
                free_socket_lk(h);
            }
            return;
        }
        if (ctx->_connecting)  {  //CONNECT      
            ctx->_connecting = false;
            if (error) report_error(Win32Error(error), "connect");
            ctx->_error = error != 0;
            ctx->_clear_to_send = true;
            auto srv = std::exchange(ctx->_send_cb, nullptr);
            invoke_cb_lk(lk, h, [&]{if (srv) srv->clear_to_send();});
        } else {    //SEND
            if (error == 0 && transfered < ctx->_to_send) {
                auto e = std::move(ctx->_aux_buffer+transfered, ctx->_aux_buffer+ctx->_to_send, ctx->_aux_buffer);
                ctx->_to_send = static_cast<DWORD>(std::distance(ctx->_aux_buffer, e));
                ZeroMemory(&ctx->_send_ovr, sizeof(OVERLAPPED));
                if (ctx->_is_handle) {
                    DWORD wrt;
                    BOOL r = WriteFile(ctx->_pipe_handle, ctx->_aux_buffer, ctx->_to_send, &wrt, &ctx->_send_ovr);
                    if (!r) {
                        error = GetLastError();
                        if (error == ERROR_IO_PENDING) {
                            error = 0;
                        } else {
                            report_error(Win32Error(error), "send");
                        }
                    }
                } else {
                    WSABUF bf = {ctx->_to_send, ctx->_aux_buffer};                
                    int r = WSASend(ctx->_socket, &bf, 1, NULL, 0, &ctx->_send_ovr, NULL);
                    if (r != 0) {
                        error = WSAGetLastError();
                        if (error == WSA_IO_PENDING) {
                            error = 0;
                        } else {
                            report_error(Win32Error(error), "send");
                        }
                    }
                }
            } else {
                ctx->_to_send = 0;
                ctx->_clear_to_send = true;
                ctx->_error = error != 0;
                auto peer = std::exchange(ctx->_send_cb, nullptr);
                if (peer) invoke_cb_lk(lk, h, [&]{peer->clear_to_send();});
            }
        } 
    }
    if (ovr == &ctx->_recv_ovr) {   //POLLIN
        if (ctx->_destroy_on_cancel_read) {
            ctx->_destroy_on_cancel_read = false;
            if (!ctx->_destroy_on_cancel_write) {
                free_socket_lk(h);
            }
            return;
        }
        if (ctx->_accept_socket != INVALID_SOCKET) {
            auto srv = std::exchange(ctx->_accept_cb, nullptr);
            if (error == 0) {
                sockaddr_storage *local, *remote;
                int local_sz = sizeof(local_sz), remote_sz = sizeof(remote_sz);
                mswsock.GetAcceptExSockaddrs(ctx->_aux_buffer,0,sizeof(*local)+16,sizeof(*remote)+16,
                    reinterpret_cast<sockaddr **>(&local), &local_sz, reinterpret_cast<sockaddr **>(&remote), &remote_sz);
                
                auto adrname = sockaddr_to_string(reinterpret_cast<sockaddr *>(remote));
                SocketInfo *nfo = alloc_socket_lk();
                nfo->_socket = ctx->_accept_socket;
                ctx->_accept_socket = INVALID_SOCKET;
                setSocketNonBlocking(nfo->_socket);
                nfo->_clear_to_send = true;
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(nfo->_socket), _completion_port, nfo->_ident+key_offset, 0);
                invoke_cb_lk(lk, h, [&]{if (srv) srv->on_accept(nfo->_ident, adrname);});                                
            } else {
                report_error(Win32Error(error), "accept");
            }
        } else {
            if (error) {
                report_error(Win32Error(error), "recv");
                transfered = 0;            
            }
            auto srv = std::exchange(ctx->_recv_cb, nullptr);
            std::string_view buff = {ctx->_recv_buffer.data(),transfered};
            ctx->_recv_buffer = {};
            invoke_cb_lk(lk, h, [&]{if (srv) srv->receive_complete(buff);});
        }
    }

}

void NetContextWin::receive(ConnHandle ident, std::span<char> buffer, IPeer *peer) {
     std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx || ctx->_recv_cb) return;
    ctx->_recv_buffer = buffer;
    ctx->_recv_cb = peer;
    ZeroMemory(&ctx->_recv_ovr, sizeof(OVERLAPPED));

    if (ctx->_error) {
        PostQueuedCompletionStatus(_completion_port, 0, ident+key_offset, &ctx->_recv_ovr);
        return;
    }

    if (ctx->_is_handle) {

        BOOL b = ReadFile(ctx->_pipe_handle, buffer.data(), static_cast<DWORD>(buffer.size()), NULL, &ctx->_recv_ovr);
        if (!b) {
            auto err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                ctx->_error = true;
                PostQueuedCompletionStatus(_completion_port,0,ident + key_offset, &ctx->_recv_ovr);            
            }
        }

    } else  {
        WSABUF buf = {static_cast<DWORD>(buffer.size()), buffer.data()};
        DWORD flags = 0;
        int rc= WSARecv(ctx->_socket, &buf, 1, NULL, &flags, &ctx->_recv_ovr, NULL);
        if (rc == SOCKET_ERROR) {
            auto err = GetLastError();
            if (err != WSA_IO_PENDING) {
                ctx->_error = true;
                PostQueuedCompletionStatus(_completion_port,0,ident + key_offset, &ctx->_recv_ovr);            
            }
        }
    }
}

std::size_t NetContextWin::send(ConnHandle ident, std::string_view data) {
     std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx || !ctx->_clear_to_send || ctx->_error) return 0;

    DWORD rcv = 0;
    if (ctx->_is_handle) {
        if (data.empty()) {
            CloseHandle(ctx->_pipe_handle);
            ctx->_pipe_handle = INVALID_HANDLE_VALUE;
            return 0;
        }
        ZeroMemory(&ctx->_send_ovr, sizeof(OVERLAPPED));
        data = data.substr(0, sizeof(ctx->_aux_buffer));
        std::copy(data.begin(), data.end(), ctx->_aux_buffer);
        ctx->_to_send = static_cast<DWORD>(data.size());
        BOOL b = WriteFile(ctx->_pipe_handle, ctx->_aux_buffer, ctx->_to_send, NULL, &ctx->_send_ovr);
        if (!b)  {
            auto err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                report_error(std::system_error(static_cast<int>(err), Win32ErrorCategory()), "send");
                return 0;
            }
        }        
        return data.size();
    }

    if (data.empty()) {
        shutdown(ctx->_socket, SD_SEND);
        return 0;
    }
    WSABUF buf = {static_cast<ULONG>(data.size()), const_cast<char *>(data.data())};
    int rc = WSASend(ctx->_socket, &buf, 1, &rcv,0,NULL,NULL);
    if (rc == SOCKET_ERROR) {
        auto err = WSAGetLastError();
        if (WSAEWOULDBLOCK != err) {
            report_error(std::system_error(static_cast<int>(err), Win32ErrorCategory()), "send");
            return 0;
        }
        rcv = 0; //nothing sent
    }
    data = data.substr(rcv);
    if (data.empty()) return rcv;  //all send - we are good
    data = data.substr(0,sizeof(ctx->_aux_buffer)); //store data in aux buffer (just small portion)
    ZeroMemory(&ctx->_send_ovr, sizeof(OVERLAPPED));
    if (ctx->_is_handle) {
        BOOL b = WriteFile(ctx->_pipe_handle, data.data(), static_cast<DWORD>(data.size()), NULL, &ctx->_send_ovr);
        if (!b)  {
            auto err = WSAGetLastError();
            if (ERROR_IO_PENDING != WSAGetLastError()) {
                report_error(std::system_error(static_cast<int>(err), Win32ErrorCategory()), "send");
                return 0;
            }
        }        
    } else {
        buf = {static_cast<ULONG>(data.size()), const_cast<char *>(data.data())};
        rc = WSASend(ctx->_socket, &buf, 1, NULL, 0, &ctx->_send_ovr, NULL);    //send data in overlapped mode to generate clear_to_send signal
        if (rc == SOCKET_ERROR) {
            auto err = WSAGetLastError();
            if (WSA_IO_PENDING != WSAGetLastError()) {
                report_error(std::system_error(static_cast<int>(err), Win32ErrorCategory()), "send");
                return 0;
            }
        }
    }

    ctx->_clear_to_send = false; //currently clear to send is false
    return rcv + data.size(); 
}

void NetContextWin::ready_to_send(ConnHandle ident, IPeer *peer) {
    std::unique_lock lk(_mx);   
    auto ctx = socket_by_ident(ident); 
    if (!ctx) return;
    ctx->_send_cb = peer;
    if (!ctx->_clear_to_send) return;  //if clear to send is false we just registered callback

    //if clear to send is true, generate signal through IOCP
    PostQueuedCompletionStatus(_completion_port,0,ident+key_offset,&ctx->_send_ovr);

}

void NetContextWin::accept(ConnHandle ident, IServer *server) {
    std::unique_lock lk(_mx);       auto ctx = socket_by_ident(ident); 
    if (!ctx) return;
    ctx->_accept_cb = server;
    if (ctx->_accept_socket != INVALID_SOCKET)  return; //already in accept - exit

    ZeroMemory(&ctx->_recv_ovr, sizeof(OVERLAPPED));
    SOCKET newSocket = socket(ctx->_af, SOCK_STREAM, IPPROTO_TCP);  //create socket
    if (newSocket == INVALID_SOCKET) {
        report_last_error("socket");
        return;
    }
    ctx->_accept_socket = newSocket;
    DWORD rd = 0;
    BOOL res = mswsock.AcceptEx(ctx->_socket, ctx->_accept_socket, 
                                ctx->_aux_buffer, 0, sizeof(sockaddr_storage)+16,  
                                sizeof(sockaddr_storage)+16, &rd, &ctx->_recv_ovr);
    if (!res) {
        auto err = WSAGetLastError();
        if (err != WSA_IO_PENDING) report_last_error("accept");
    } else {
        PostQueuedCompletionStatus(_completion_port, rd, ctx->_ident + key_offset, &ctx->_recv_ovr);
    } 
}

template<typename Fn>
void NetContextWin::invoke_cb_lk(std::unique_lock<std::mutex> &lk, ConnHandle id, Fn &&fn) {
    auto ctx = socket_by_ident(id);
    ++ctx->_cb_call_cntr;
    ++callback_call_counter;
    lk.unlock();
    fn();
    lk.lock();
    --callback_call_counter;
    ctx = socket_by_ident(id);
    if (--ctx->_cb_call_cntr == 0) {
        _cond.notify_all();
    }
}

bool NetContextWin::in_calback() const {
    return callback_call_counter>0;
}


std::jthread NetContextWin::run_thread() {
    return std::jthread([this](auto tkn){
        run(std::move(tkn));
    });
}

NetContextWin::SocketInfo *NetContextWin::wait_for_finish_cbs_lk(std::unique_lock<std::mutex> &lk, ConnHandle id) {
    SocketInfo *nfo = {};
    _cond.wait(lk, [&]{
        nfo = socket_by_ident(id);
        return nfo == nullptr || nfo->_cb_call_cntr == 0;
    });
    return nfo;
}

void NetContextWin::destroy(ConnHandle ident) {
    std::unique_lock lk(_mx);    
    auto ctx = wait_for_finish_cbs_lk(lk, ident);
    if (!ctx) return;
    if (!ctx->_clear_to_send || ctx->_connecting) ctx->_destroy_on_cancel_write = true;
    if (ctx->_accept_socket != INVALID_SOCKET || !ctx->_recv_buffer.empty()) ctx->_destroy_on_cancel_read = true;
    if (ctx->_is_handle) CloseHandle(ctx->_pipe_handle); else closesocket(ctx->_socket);
    if (ctx->_accept_cb) closesocket(ctx->_accept_socket);
    ctx->_accept_cb = nullptr;
    ctx->_recv_cb = nullptr;
    ctx->_send_cb = nullptr;
    ctx->_timeout_cb = nullptr;
    _tmset.erase({ctx->_tmtp, ident});
    if (!ctx->_destroy_on_cancel_read && !ctx->_destroy_on_cancel_write) free_socket_lk(ident);
}

void NetContextWin::run(std::stop_token tkn) {
    run_worker(std::move(tkn));
}

void NetContextWin::set_timeout(ConnHandle ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    auto top = get_completion_timeout_tp_lk();
    _tmset.erase({ctx->_tmtp, ident});
    ctx->_timeout_cb = p;
    ctx->_tmtp = tp;
    _tmset.insert({ctx->_tmtp, ident});
    bool ntf = top > tp;
    if (ntf) {
        PostQueuedCompletionStatus(_completion_port, 0, key_wakeup, NULL);
    }
}

void NetContextWin::clear_timeout(ConnHandle ident) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    _tmset.erase({ctx->_tmtp, ident});

}

void NetContextWin::enqueue(SimpleAction fn)
{
    std::lock_guard _(_mx);
    _actions.push_back(std::move(fn));
    PostQueuedCompletionStatus(_completion_port,0,key_wakeup,nullptr);
}

ConnHandle NetContextWin::connect(SpecialConnection type, const void *arg)
{
    auto ctx = alloc_socket_lk();
    HANDLE h;
    switch (type) {
        default:
        case SpecialConnection::null:  break;
        case SpecialConnection::descriptor: DuplicateHandle(
            GetCurrentProcess(),*reinterpret_cast<const HANDLE *>(arg),GetCurrentProcess(),
            &ctx->_pipe_handle,0,FALSE,DUPLICATE_SAME_ACCESS);
            CreateIoCompletionPort(ctx->_pipe_handle, _completion_port, ctx->_ident+key_offset, 0);
            ctx->_is_handle = true;
            break;
        case SpecialConnection::socket: 
            ctx->_socket = *reinterpret_cast<const SOCKET *>(arg);
            CreateIoCompletionPort(reinterpret_cast<HANDLE>(ctx->_pipe_handle), _completion_port, ctx->_ident+key_offset, 0);
            break;
        case SpecialConnection::stdinput:
            h = GetStdHandle(STD_INPUT_HANDLE);
            return connect(SpecialConnection::descriptor, &h);
        case SpecialConnection::stdoutput:
            h = GetStdHandle(STD_OUTPUT_HANDLE);
            return connect(SpecialConnection::descriptor, &h);
        case SpecialConnection::stderror:
            h = GetStdHandle(STD_ERROR_HANDLE);
            return connect(SpecialConnection::descriptor, &h);        
    
    }
    ctx->_clear_to_send = true;
   return ctx->_ident;
}


static volatile long PipeSerialNumber;

BOOL CreatePipeEx(
    OUT LPHANDLE lpReadPipe,
    OUT LPHANDLE lpWritePipe,
    IN LPSECURITY_ATTRIBUTES lpPipeAttributes,
    IN DWORD nSize,
    DWORD dwReadMode,
    DWORD dwWriteMode
    ) {
  HANDLE ReadPipeHandle, WritePipeHandle;
  DWORD dwError;
  wchar_t PipeNameBuffer[ MAX_PATH ];

  //
  // Only one valid OpenMode flag - FILE_FLAG_OVERLAPPED
  //

  if ((dwReadMode | dwWriteMode) & (~FILE_FLAG_OVERLAPPED)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  //
  //  Set the default timeout to 120 seconds
  //

  if (nSize == 0) {
    nSize = 4096;
  }

  wsprintfW( PipeNameBuffer,
           L"\\\\.\\Pipe\\AnonPipe.%08x.%08x",
           GetCurrentProcessId(),
           InterlockedIncrement(&PipeSerialNumber)
         );

  ReadPipeHandle = CreateNamedPipeW(
                       PipeNameBuffer,
                       PIPE_ACCESS_INBOUND | dwReadMode,
                       PIPE_TYPE_BYTE | PIPE_WAIT,
                       1,             // Number of pipes
                       nSize,         // Out buffer size
                       nSize,         // In buffer size
                       120 * 1000,    // Timeout in ms
                       lpPipeAttributes
                       );

  if (! ReadPipeHandle) {
    return FALSE;
  }

  WritePipeHandle = CreateFileW(
                      PipeNameBuffer,
                      GENERIC_WRITE,
                      0,                         // No sharing
                      lpPipeAttributes,
                      OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | dwWriteMode,
                      NULL                       // Template file
                    );

  if (INVALID_HANDLE_VALUE == WritePipeHandle) {
    dwError = GetLastError();
    CloseHandle( ReadPipeHandle );
    SetLastError(dwError);
    return FALSE;
  }

  *lpReadPipe = ReadPipeHandle;
  *lpWritePipe = WritePipeHandle;
  return( TRUE );
}

PipePair NetContextWin::create_pipe()
{
    HANDLE read_end;
    HANDLE write_end;
    if (!CreatePipeEx(&read_end, &write_end, NULL, 0, FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED)) report_last_error("CreatePipe");
    auto cread = connect(SpecialConnection::descriptor,&read_end);
    auto cwrite = connect(SpecialConnection::descriptor,&write_end);
    CloseHandle(read_end);
    CloseHandle(write_end);
    return {cread, cwrite};
}


template <typename E>
inline void NetContextWin::report_error(E exception, std::string_view action, std::source_location loc)
{
    try {
        throw exception;
    } catch (...){
        _ecb(action, loc);
    }
}

void NetContextWin::report_last_error(std::string_view action, std::source_location loc)
{
    report_error(Win32Error(), action, loc);
}



Win32Error::Win32Error():std::system_error(static_cast<int>(GetLastError()), Win32ErrorCategory()) {}
Win32Error::Win32Error(std::string message):std::system_error(static_cast<int>(GetLastError()), Win32ErrorCategory(), message) {}
Win32Error::Win32Error(DWORD error):std::system_error(static_cast<int>(error), Win32ErrorCategory()) {}
Win32Error::Win32Error(DWORD error, std::string message):std::system_error(static_cast<int>(error), Win32ErrorCategory(), message) {}

static std::string GetErrorMessage(int _Errval) {
    wchar_t *s = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
               NULL, static_cast<DWORD>(_Errval),
               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
               (LPWSTR)&s, 0, NULL);

    std::size_t sz = wcslen(s);
    std::size_t needsz = WideCharToMultiByte(CP_UTF8,0,s,static_cast<int>(sz),NULL,0,NULL,NULL);
    std::string out;
    out.resize(needsz);
    WideCharToMultiByte(CP_UTF8,0,s,static_cast<int>(sz),out.data(),static_cast<int>(out.size()),NULL,NULL);
    LocalFree(s);
    out.append("(Error code=").append(std::to_string(_Errval)).append(")");
    return out;
}

const char *Win32ErrorCategory::name() const noexcept {return "Win32 Error";}
std::string Win32ErrorCategory::message(int _Errval) const {return GetErrorMessage(_Errval);}


class ProcessMonitor: public IPeer {
public:
    ProcessMonitor(HANDLE hProcess,
                   DWORD pid, 
                   std::stop_token tkn, 
                   std::function<void(int)> exitfn, 
                   std::shared_ptr<INetContext> ctx, 
                   ConnHandle aux_pipe)
        :_hProcess(hProcess)
        ,_pid(pid)
        ,_ctx(std::move(ctx))
        ,_exitfn(std::move(exitfn))
        ,_aux_pipe(aux_pipe)
        ,_stopcb(tkn,StopCb {this}) {
            if (_ctx) {
                _ctx->receive(_aux_pipe, {_dummy_buff, sizeof(_dummy_buff)}, this);
            }
        }

    virtual void clear_to_send() noexcept {}
    virtual void on_timeout() noexcept {}
    virtual void receive_complete(std::string_view) noexcept {

        if (WaitForSingleObject(_hProcess, 2000) == WAIT_TIMEOUT) {
            TerminateProcess(_hProcess, 255);
        }
        DWORD exitCode;
        GetExitCodeProcess(_hProcess, &exitCode);

        if (_exitfn) {
            _exitfn(static_cast<int>(exitCode));
        }
        delete this;
    }


    ~ProcessMonitor() {        
        if (_ctx) _ctx->destroy(_aux_pipe);
        CloseHandle (_hProcess);
    }

protected:

    struct StopCb {
        ProcessMonitor *_owner;
        void operator()() const{
            _owner->on_stop();
        }

    };

    HANDLE _hProcess;
    DWORD _pid;
    std::function<void(int)> _exitfn;
    std::shared_ptr<INetContext> _ctx;
    ConnHandle _aux_pipe;
    std::stop_callback<StopCb> _stopcb;
    char _dummy_buff[4];

    void on_stop() {
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, _pid);
        if (!_ctx) delete this;
    }

    
};



PipePair spawn_process(std::shared_ptr<INetContext> ctx,
                        std::string_view command_line,
                        std::stop_token tkn ,
                        std::function<void(int)> exit_action) {

    HANDLE myIn =INVALID_HANDLE_VALUE, theirOut = INVALID_HANDLE_VALUE;
    HANDLE myOut = INVALID_HANDLE_VALUE, theirIn = INVALID_HANDLE_VALUE;
    HANDLE monIn = INVALID_HANDLE_VALUE, monOut = INVALID_HANDLE_VALUE;
    std::optional<ConnHandle> mon_handle;
    
    try {
        if (!CreatePipeEx(&myIn, &theirOut,NULL, 0, FILE_FLAG_OVERLAPPED,FILE_FLAG_OVERLAPPED)) throw Win32Error();
        if (!CreatePipeEx(&theirIn, &myOut,NULL, 0, FILE_FLAG_OVERLAPPED,FILE_FLAG_OVERLAPPED)) throw Win32Error();
        if (exit_action) {
            if (!CreatePipeEx(&monIn, &monOut,NULL, 0, FILE_FLAG_OVERLAPPED,FILE_FLAG_OVERLAPPED)) throw Win32Error();
            mon_handle = ctx->connect(SpecialConnection::descriptor, &monIn);
            CloseHandle(monIn); monIn = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(GetCurrentProcess(), monOut, GetCurrentProcess(), &monOut, 0,TRUE, DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE))
                throw Win32Error();
            
        }

        std::wstring cmdline;
        cmdline.resize(MultiByteToWideChar(CP_UTF8, 0, command_line.data(), static_cast<DWORD>(command_line.size()), 0,0)+1);
        MultiByteToWideChar(CP_UTF8, 0, command_line.data(), static_cast<DWORD>(command_line.size()), cmdline.data(),static_cast<DWORD>(cmdline.size()));

        PROCESS_INFORMATION pi = {};
        STARTUPINFOW si = {};
        si.cb = sizeof(si);

        if (!DuplicateHandle(GetCurrentProcess(), theirIn, GetCurrentProcess(), &theirIn, 0,TRUE, DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE)) {
            throw Win32Error();
        }
        if (!DuplicateHandle(GetCurrentProcess(), theirOut, GetCurrentProcess(), &theirOut, 0,TRUE, DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE)) {
            throw Win32Error();
        }
        si.hStdInput = theirIn;
        si.hStdOutput = theirOut;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.dwFlags = STARTF_USESTDHANDLES;

        BOOL b = CreateProcessW(NULL, cmdline.data(), NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP|CREATE_SUSPENDED, NULL, NULL, &si, &pi);
        if (!b) throw Win32Error();
        if (tkn.stop_possible()) {
            if (mon_handle) {
                new ProcessMonitor(pi.hProcess, pi.dwProcessId, tkn,std::move(exit_action), ctx, *mon_handle);
                mon_handle.reset();
                CloseHandle(monOut);
            } else {
                new ProcessMonitor(pi.hProcess, pi.dwProcessId, tkn, {}, {}, {});
            }
        } else if (mon_handle) {
            new ProcessMonitor(pi.hProcess, pi.dwProcessId, tkn, std::move(exit_action), ctx, *mon_handle);
            mon_handle.reset();
            CloseHandle(monOut);
        } else {
            CloseHandle(pi.hProcess);
        }
        
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);

        PipePair out = {};
        out.read = ctx->connect(SpecialConnection::descriptor, &myIn);
        out.write = ctx->connect(SpecialConnection::descriptor, &myOut);

        CloseHandle(theirIn);
        CloseHandle(theirOut);
        CloseHandle(myIn);
        CloseHandle(myOut);

        return out;

    }catch (...){
        if (myIn && myIn != INVALID_HANDLE_VALUE) CloseHandle(myIn);
        if (myOut && myOut != INVALID_HANDLE_VALUE) CloseHandle(myOut);
        if (theirIn && theirIn != INVALID_HANDLE_VALUE) CloseHandle(theirIn);
        if (theirOut && theirOut != INVALID_HANDLE_VALUE) CloseHandle(theirOut);
        if (monIn && monIn != INVALID_HANDLE_VALUE) CloseHandle(monIn);
        if (monOut && monOut != INVALID_HANDLE_VALUE) CloseHandle(monOut);
        if (mon_handle) ctx->destroy(*mon_handle);
        throw;
    }


}


}
