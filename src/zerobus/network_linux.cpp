#include "network_linux.h"
#include <utility>

#include <arpa/inet.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>

namespace zerobus {


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

        case AF_UNIX: {  // Unix socket
            const sockaddr_un* unix_addr = reinterpret_cast<const sockaddr_un*>(addr);
            return "unix:" + std::string(unix_addr->sun_path);
        }

        default:
            return "unknown";
    }
}


std::chrono::system_clock::time_point NetContext::get_epoll_timeout_lk() {
    if (!_tmset.empty()) {
        return _tmset.begin()->first;
    }
    return std::chrono::system_clock::time_point::max();
}

NetContext::SocketInfo *NetContext::alloc_socket_lk() {
    if (_first_free_socket_ident >= _sockets.size()) {
        _sockets.resize(_first_free_socket_ident+1);
        _sockets.back()._ident = _sockets.size();
    }
    SocketInfo *nfo = &_sockets[_first_free_socket_ident];
    std::swap(nfo->_ident,_first_free_socket_ident);
    return nfo;
}

void NetContext::free_socket_lk(SocketIdent id) {
    SocketInfo *nfo = &_sockets[id];
    std::destroy_at(nfo);
    std::construct_at(nfo);
    nfo->_ident = _first_free_socket_ident;
    _first_free_socket_ident = id;
}

NetContext::SocketInfo *NetContext::socket_by_ident(SocketIdent id) {
    if (id >= _sockets.size()) return nullptr;
    auto r = &_sockets[id];
    return r->_ident == id?r:nullptr;
}


void NetContext::run_worker(std::stop_token tkn, int efd)  {
    std::unique_lock lk(_mx);
    std::stop_callback __(tkn, [&]{
        eventfd_write(efd, 1);
    });

    _epoll.add(efd, EPOLLIN, -1);

    while (!tkn.stop_requested()) {
        auto timeout = std::chrono::system_clock::time_point::max();
        int needfd = -1;
        bool timeout_thread = false;
        if (_cur_timer_thread.compare_exchange_strong(needfd, efd)) {
            timeout = get_epoll_timeout_lk();
            timeout_thread= true;
        }
        auto res = _epoll.wait(timeout);
        if (timeout_thread) {
            _cur_timer_thread = -1;
        }
        if (!res) {
            auto now = std::chrono::system_clock::now();
            while (!_tmset.empty()) {
                auto iter = _tmset.begin();
                if (iter->first > now) break;
                SocketIdent id = iter->second;
                _tmset.erase(iter);
                SocketInfo *nfo = socket_by_ident(id);
                if (nfo && nfo->_timeout_cb) {
                    auto cb = std::exchange(nfo->_timeout_cb, nullptr);
                    nfo->invoke_cb(lk, _cond, [&]{cb->on_timeout();});
                }
            }
        } else {
            auto &e = *res;
            if (e.ident >= 0) {
                process_event_lk(lk, e);
            } else {
                eventfd_t dummy;
                eventfd_read(efd, &dummy);
            }
            if (tkn.stop_requested()) break;
        }
    }

}

void NetContext::receive(SocketIdent ident, std::span<char> buffer, IPeer *peer) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    ctx->_flags |= EPOLLIN;
    ctx->_recv_buffer = buffer;
    ctx->_recv_cb = peer;
    apply_flags_lk(ctx);
}

std::size_t NetContext::send(SocketIdent ident, std::string_view data) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return 0;
    int s = ::send(ctx->_socket, data.data(), data.size(), MSG_DONTWAIT);
    if (s < 0) {
        int e = errno;
        if (e == EWOULDBLOCK || e == EPIPE || e == ECONNRESET) {
            s = 0;
        } else {
            throw std::system_error(e, std::system_category(), "recv failed");
        }
    }
    return s;
}

void NetContext::callback_on_send_available(SocketIdent ident, IPeer *peer) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    ctx->_flags |= EPOLLOUT;
    ctx->_send_cb = peer;
    apply_flags_lk(ctx);
}

SocketIdent NetContext::create_server(std::string address_port) {
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

    hints.ai_family = AF_UNSPEC;       // IPv4 nebo IPv6
    hints.ai_socktype = SOCK_STREAM;   // TCP
    hints.ai_flags = AI_PASSIVE;       // Použít pro bind()

    int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (status != 0) {
        throw std::invalid_argument("Invalid address or port: " + std::string(gai_strerror(status)));
    }

    int listen_fd = -1;

    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd == -1) {
            continue;
        }

        int opt = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            close(listen_fd);
            freeaddrinfo(res);
            throw std::system_error(errno, std::generic_category(), "Failed to set socket options");
        }

        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(listen_fd);
            listen_fd = -1;
            continue;
        }

        break;
    }

    freeaddrinfo(res);

    if (listen_fd == -1) {
        throw std::system_error(errno, std::generic_category(), "Failed to bind to address");
    }

    if (listen(listen_fd, SOMAXCONN) == -1) {
        close(listen_fd);
        throw std::system_error(errno, std::generic_category(), "Failed to listen on socket");
    }

    std::lock_guard _(_mx);
    SocketInfo *nfo = alloc_socket_lk();
    nfo->_socket = listen_fd;
    _epoll.add(listen_fd, 0, nfo->_ident);
    return nfo->_ident;

}

void NetContext::accept(SocketIdent ident, IServer *server) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    ctx->_flags |= EPOLLIN;
    ctx->_accept_cb = server;
    apply_flags_lk(ctx);
}

void NetContext::destroy(SocketIdent ident) {
    std::unique_lock lk(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    _cond.wait(lk, [&]{return ctx->_cbprotect == 0;}); //wait for finishing all callbacks
    lk.lock();
    _epoll.del(ctx->_socket);
    ::close(ctx->_socket);
    _tmset.erase({ctx->_tmtp, ident});
    free_socket_lk(ident);
}


static int connect_peer(std::string address_port) {
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

    int sockfd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, p->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
                close(sockfd);
                sockfd = -1;
                continue;
            }
        }
        break;
    }

    freeaddrinfo(res);

    if (sockfd == -1) {
        throw std::system_error(errno, std::generic_category(), "Failed to connect");
    }
    return sockfd;
}


SocketIdent NetContext::peer_connect(std::string address_port)  {

    int sockfd = connect_peer(address_port);
    std::lock_guard _(_mx);
    SocketInfo *nfo = alloc_socket_lk();
    nfo->_socket = sockfd;
    _epoll.add(sockfd, 0, nfo->_ident);
    return nfo->_ident;
}

 void NetContext::reconnect(SocketIdent ident, std::string address_port) {
     std::lock_guard _(_mx);
     auto ctx = socket_by_ident(ident);
     if (!ctx) return;
     auto newfd = connect_peer(std::move(address_port));
     _epoll.del(ctx->_socket);
     ::close(ctx->_socket);
     ctx->_socket = newfd;
     _epoll.add(ctx->_socket, 0, ident);
     ctx->_flags = 0;
     ctx->_cur_flags = 0;
 }




std::jthread NetContext::run_thread() {
    return std::jthread([this](auto tkn){
        run(std::move(tkn));
    });
}

void NetContext::run(std::stop_token tkn) {
    auto me = shared_from_this();
    int efd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
    try {
        run_worker(std::move(tkn), efd);
        ::close(efd);
    } catch(...) {
        ::close(efd);
        throw;
    }
}




void NetContext::set_timeout(SocketIdent ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    auto top = get_epoll_timeout_lk();
    _tmset.erase({ctx->_tmtp, ident});
    ctx->_timeout_cb = p;
    ctx->_tmtp = tp;
    _tmset.insert({ctx->_tmtp, ident});
    bool ntf = top > tp;
    if (ntf && _cur_timer_thread >= 0) {
        eventfd_write(_cur_timer_thread, 1);
    }
}

void NetContext::clear_timeout(SocketIdent ident) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    _tmset.erase({ctx->_tmtp, ident});

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




void NetContext::process_event_lk(std::unique_lock<std::mutex> &lk, const WaitRes &e) {
    auto ctx = socket_by_ident(e.ident);
    if (!ctx) return;
    ctx->_cur_flags = 0;
    if (e.events & EPOLLIN) {
        if (ctx->_accept_cb) {
            auto srv = std::exchange(ctx->_accept_cb, nullptr);
            sockaddr_storage saddr_stor;
            socklen_t slen = sizeof(saddr_stor);
            sockaddr *saddr = reinterpret_cast<sockaddr *>(&saddr_stor);
            auto n = accept4(ctx->_socket, saddr, &slen, SOCK_CLOEXEC|SOCK_NONBLOCK);
            if (n >= 0) {
                SocketInfo *nfo = alloc_socket_lk();
                nfo->_socket = n;
                _epoll.add(n,0, nfo->_ident);;

                ctx->invoke_cb(lk, _cond, [&]{if (srv) srv->on_accept(nfo->_ident, sockaddr_to_string(saddr));});
            }
        }
        if (ctx->_recv_cb) {
            int r;
            do {
                auto peer = std::exchange(ctx->_recv_cb, nullptr);
                if (!peer) break;
                r = recv(ctx->_socket, ctx->_recv_buffer.data(), ctx->_recv_buffer.size(), MSG_DONTWAIT);
                if (r < 0) {
                    int e = errno;
                    if (e == EWOULDBLOCK) {
                        ctx->_flags |= EPOLLIN;
                    } else {
                        ctx->invoke_cb(lk, _cond,  [&]{peer->on_read_complete({});});
                    }
                } else {
                    ctx->invoke_cb(lk, _cond, [&]{peer->on_read_complete(std::string_view(ctx->_recv_buffer.data(), r));});
                }
            } while (r>0);
        }
    }
    if (e.events & EPOLLOUT) {
        auto peer = std::exchange(ctx->_send_cb, nullptr);
        if (peer) ctx->invoke_cb(lk, _cond, [&]{peer->on_send_available();});
    }
    apply_flags_lk(ctx);

}

void NetContext::apply_flags_lk(SocketInfo *ctx) {
    if (ctx->_flags != ctx->_cur_flags) {
        _epoll.mod(ctx->_socket, ctx->_flags, ctx->_ident);
        ctx->_cur_flags = ctx->_flags;
    }
}


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




}
