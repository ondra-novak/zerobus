#include "network_windows.h"
#include <MSWSock.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")


namespace zerobus {

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




std::shared_ptr<INetContext> make_context(int iothreads) {
    auto p = std::make_shared<NetThreadedContext>(iothreads);
    p->start();
    return p;
}

NetThreadedContext::NetThreadedContext(int threads)
    :_threads(threads)
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


ConnHandle NetContext::create_server(std::string address_port) {
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


NetContext::SocketInfo *NetContext::alloc_socket_lk() {
    if (_first_free_socket_ident >= _sockets.size()) {
        _sockets.resize(_first_free_socket_ident+1);
        _sockets.back()._ident = static_cast<ConnHandle>(_sockets.size());
    }
    SocketInfo *nfo = &_sockets[_first_free_socket_ident];
    std::swap(nfo->_ident,_first_free_socket_ident);
    return nfo;
}


NetContext::NetContext() {
    _completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);    
}

NetContext::~NetContext() {
    CloseHandle(_completion_port);
}

void setSocketNonBlocking(SOCKET sock) {
    u_long mode = 1; 
    std::ignore = ioctlsocket(sock, FIONBIO, &mode);
}

ConnHandle NetContext::connect_peer(std::string address_port) {
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

    auto ctx = alloc_socket_lk();

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
        CreateIoCompletionPort(reinterpret_cast<HANDLE>(sockfd), _completion_port, ctx->_ident+key_offset, 0);
        ZeroMemory(&ctx->_send_ovl, sizeof(ctx->_send_ovl));
        if (!mswsock.ConnectEx(sockfd,p->ai_addr, static_cast<int>(p->ai_addrlen), NULL, 0, NULL, &ctx->_send_ovl)) {
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
        free_socket_lk(ctx->_ident);
        throw std::system_error(errno, std::generic_category(), "Failed to connect");
    }

    ctx->_clear_to_send = false;    
    ctx->_socket = sockfd;
    return ctx->_ident;
}


void NetContext::free_socket_lk(ConnHandle id) {
    SocketInfo *nfo = &_sockets[id];
    std::destroy_at(nfo);
    std::construct_at(nfo);
    nfo->_ident = _first_free_socket_ident;
    _first_free_socket_ident = id;
}


NetContext::SocketInfo *NetContext::socket_by_ident(ConnHandle id) {
    if (id >= _sockets.size()) return nullptr;
    auto r = &_sockets[id];
    return r->_ident == id?r:nullptr;
}


ConnHandle NetContext::peer_connect(std::string address_port)  {
    std::lock_guard _(_mx);
    return connect_peer(address_port);
}

 void NetContext::reconnect(ConnHandle ident, std::string address_port) {
     std::lock_guard _(_mx);
     auto ctx = socket_by_ident(ident);
     if (!ctx) return;
     auto newh = connect_peer(std::move(address_port));
     auto newctx = socket_by_ident(newh);
     closesocket(ctx->_socket);
     ctx->_socket = newctx->_socket;
     ctx->_connecting = true;
     ctx->_clear_to_send = false;
 }



void NetContext::run_worker(std::stop_token tkn)  {
    std::unique_lock lk(_mx);
    std::stop_callback __(tkn, [&]{
        PostQueuedCompletionStatus(_completion_port,0,key_exit,NULL);
    });

    std::vector<std::function<void()> > yield_queue;
    yield_queue.reserve(4);   

    while (!tkn.stop_requested()) {
        DWORD timeout = INFINITE;
        DWORD transfered;
        ULONG_PTR key;
        LPOVERLAPPED ovr;

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
                    nfo->invoke_cb(lk, _cond, [&]{cb->on_timeout();});
                    if (nfo->_yield) {
                        yield_queue.push_back(std::move(nfo->_yield));
                    }
                }
            }
        } else {
            if (key >= key_offset) {
                auto err = GetLastError();
                if (res) err = 0;
                process_event_lk(lk, static_cast<ConnHandle>(key-key_offset), transfered, ovr, err, yield_queue);
            }
        }
        lk.unlock();
        for (auto &x: yield_queue) x();
        yield_queue.clear();
        if (tkn.stop_requested()) break;
        lk.lock();
    }

}

DWORD NetContext::get_completion_timeout_lk()
{
    auto tp = get_completion_timeout_tp_lk();
    if (tp == std::chrono::system_clock::time_point::max()) return INFINITE;
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp - std::chrono::system_clock::now()).count();
    if (diff > static_cast<decltype(diff)>(std::numeric_limits<DWORD>::max())) {
        return INFINITE-1;
    } 
    return static_cast<DWORD>(diff);
}

std::chrono::system_clock::time_point NetContext::get_completion_timeout_tp_lk()
{
    if (!_tmset.empty()) {
        return _tmset.begin()->first;
    }
    return std::chrono::system_clock::time_point::max();
}

void NetContext::process_event_lk(std::unique_lock<std::mutex> &lk, ConnHandle h,  DWORD transfered, OVERLAPPED *ovr, DWORD error,  std::vector<std::function<void()> > &yield_queue) {
    auto ctx = socket_by_ident(h);
    if (!ctx) return;    
    if (ovr == &ctx->_send_ovl) {   //POLLOUT
        if (ctx->_connecting)  {  //CONNECT
            ctx->_connecting = false;
            ctx->_error = error != 0;
            ctx->_clear_to_send = true;
            auto srv = std::exchange(ctx->_send_cb, nullptr);
            ctx->invoke_cb(lk, _cond, [&]{if (srv) srv->clear_to_send();});
        } else {    //SEND
            if (error == 0 && transfered < ctx->_aux_to_send) {
                auto e = std::move(ctx->_aux_buffer+transfered, ctx->_aux_buffer+ctx->_aux_to_send, ctx->_aux_buffer);
                ctx->_aux_to_send = static_cast<DWORD>(std::distance(ctx->_aux_buffer, e));
                WSABUF bf = {ctx->_aux_to_send, ctx->_aux_buffer};                
                int r = WSASend(ctx->_socket, &bf, 1, NULL, 0, &ctx->_send_ovl, NULL);
                if (r != 0) {
                    error = WSAGetLastError();
                    if (error != WSA_IO_PENDING) {
                        error = 0;
                    }
                }
            }
            ctx->_clear_to_send = true;
            ctx->_error = error != 0;
            auto peer = std::exchange(ctx->_send_cb, nullptr);
            if (peer) ctx->invoke_cb(lk, _cond, [&]{peer->clear_to_send();});
        } 
    }
    if (ovr == &ctx->_recv_ovl) {   //POLLIN
        if (ctx->_accept_cb) {
            auto srv = std::exchange(ctx->_accept_cb, nullptr);
            if (error == 0) {
                sockaddr_storage *local, *remote;
                int local_sz = sizeof(local_sz), remote_sz = sizeof(remote_sz);
                mswsock.GetAcceptExSockaddrs(ctx->_aux_buffer,0,sizeof(*local)+16,sizeof(*remote)+16,
                    reinterpret_cast<sockaddr **>(&local), &local_sz, reinterpret_cast<sockaddr **>(&remote), &remote_sz);
                
                auto adrname = sockaddr_to_string(reinterpret_cast<sockaddr *>(remote));
                SocketInfo *nfo = alloc_socket_lk();
                ctx = socket_by_ident(h); //reallocation of socket list, we must find ctx again
                nfo->_socket = ctx->_accept_socket;
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(nfo->_socket), _completion_port, nfo->_ident+key_offset, 0);
                ctx->invoke_cb(lk, _cond, [&]{if (srv) srv->on_accept(nfo->_ident, adrname);});
            } else {
                ctx->invoke_cb(lk, _cond, [&]{if (srv) srv->on_accept(static_cast<ConnHandle>(-1), {});});
            }
        } else {
            if (error) transfered = 0;            
            auto srv = std::exchange(ctx->_recv_cb, nullptr);
            ctx->invoke_cb(lk, _cond, [&]{if (srv) srv->receive_complete({ctx->_recv_buffer.data(),transfered});});
        }
    }
    if (ctx->_yield) {
        yield_queue.push_back(std::move(ctx->_yield));
    }

}

void NetContext::yield(ConnHandle connection, std::function<void()> fn) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(connection);
    if (ctx) ctx->_yield = std::move(fn);
}

bool NetContext::sync_wait(ConnHandle connection,
        std::atomic<std::uintptr_t> &var, std::uintptr_t block_value,
        std::chrono::system_clock::time_point timeout) {
    std::unique_lock lk(_mx);
    while (var.load() == block_value) {
        if (socket_by_ident(connection) == nullptr) return false;
        if (_cond.wait_until(lk, timeout) == std::cv_status::timeout) return false;
    }
    return true;
}

void NetContext::sync_notify(ConnHandle ) {
    std::unique_lock lk(_mx);   //must be under lock
    _cond.notify_all();

}

void NetContext::receive(ConnHandle ident, std::span<char> buffer, IPeer *peer) {
     std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx || ctx->_recv_cb) return;
    WSABUF buf = {static_cast<DWORD>(buffer.size()), buffer.data()};
    ZeroMemory(&ctx->_recv_ovl, sizeof(OVERLAPPED));
    int rc= WSARecv(ctx->_socket, &buf, 1, NULL, 0, &ctx->_recv_ovl, NULL);
    ctx->_recv_cb = peer;
    if (rc == SOCKET_ERROR && WSA_IO_PENDING != WSAGetLastError()) {
        PostQueuedCompletionStatus(_completion_port,0,ident + key_offset, NULL);
    }    

}

std::size_t NetContext::send(ConnHandle ident, std::string_view data) {
     std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx || !ctx->_clear_to_send) return 0;
    WSABUF buf = {static_cast<ULONG>(data.size()), const_cast<char *>(data.data())};
    DWORD rcv = 0;
    int rc = WSASend(ctx->_socket, &buf, 1, &rcv,0,NULL,NULL);
    if (rc == SOCKET_ERROR) {
        if (WSAEWOULDBLOCK != WSAGetLastError()) {
            return 0;
        }
    }
    data = data.substr(rcv);
    if (data.empty()) return rcv;
    data = data.substr(0,sizeof(ctx->_aux_buffer));
    buf.buf = const_cast<char *>(data.data());
    buf.len = static_cast<ULONG>(data.size());
    ZeroMemory(&ctx->_recv_ovl, sizeof(OVERLAPPED));
    rc = WSASend(ctx->_socket, &buf, 1, NULL, 0, &ctx->_recv_ovl, NULL);
    if (rc == SOCKET_ERROR) {
        if (WSA_IO_PENDING != WSAGetLastError()) {
            return 0;
        }
    }
    return rcv + buf.len;
}

void NetContext::ready_to_send(ConnHandle ident, IPeer *peer) {
    std::unique_lock lk(_mx);   
    auto ctx = socket_by_ident(ident); 
    if (!ctx) return;
    ctx->_send_cb = peer;
    if (!ctx->_clear_to_send) return;
    ctx->invoke_cb(lk,_cond,[&]{ctx->_send_cb->clear_to_send();});
}

void NetContext::accept(ConnHandle ident, IServer *server) {
    std::unique_lock lk(_mx);   
    auto ctx = socket_by_ident(ident); 
    if (!ctx) return;
    if (ctx->_accept_cb) {
        ctx->_accept_cb = server;
        return;
    }
    ZeroMemory(&ctx->_recv_ovl, sizeof(OVERLAPPED));
    SOCKET newSocket = socket(ctx->_af, SOCK_STREAM, IPPROTO_TCP);
    if (newSocket == INVALID_SOCKET) return;
    ctx->_accept_socket = newSocket;
    DWORD rd = 0;
    BOOL res = mswsock.AcceptEx(ctx->_socket, ctx->_accept_socket, 
                                ctx->_aux_buffer, 0, sizeof(sockaddr_storage)+16,  
                                sizeof(sockaddr_storage)+16, &rd, &ctx->_recv_ovl);
    if (res) {
        PostQueuedCompletionStatus(_completion_port, rd, ctx->_ident + key_offset, &ctx->_recv_ovl);
    } 
}

template<typename Fn>
void NetContext::SocketInfo::invoke_cb(std::unique_lock<std::mutex> &lk, std::condition_variable &cond, Fn &&fn) {
    ++_cbprotect;
    lk.unlock();
    fn();
    lk.lock();
    if (--_cbprotect == 0) {
        cond.notify_all();
    }
}

std::jthread NetContext::run_thread() {
    return std::jthread([this](auto tkn){
        run(std::move(tkn));
    });
}

void NetContext::destroy(ConnHandle ident) {
    std::unique_lock lk(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    _cond.wait(lk, [&]{return ctx->_cbprotect == 0;}); //wait for finishing all callbacks
    closesocket(ctx->_socket);
    if (ctx->_accept_cb) closesocket(ctx->_accept_socket);
    _tmset.erase({ctx->_tmtp, ident});
    free_socket_lk(ident);
}

void NetContext::run(std::stop_token tkn) {
    run_worker(std::move(tkn));
}

void NetContext::set_timeout(ConnHandle ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) {
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

void NetContext::clear_timeout(ConnHandle ident) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    _tmset.erase({ctx->_tmtp, ident});

}


}