#include "network_linux.h"
#include "utility.h"

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

NetContext::NetContext() {
    _epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (_epollfd < 0) {
        int e = errno;
        throw std::system_error(e, std::system_category(), "epoll creation failed");
    }
}

NetContext::~NetContext() {
    ::close(_epollfd);

}

int NetContext::get_epoll_timeout() {
    std::lock_guard _(_tmx);
    auto now = std::chrono::system_clock::now();
    if (!_pqueue.empty()) {
        auto nx = _pqueue.front().get_tp();
        return nx < now?0:std::chrono::duration_cast<std::chrono::milliseconds>(nx - now).count();
    }
    return -1;

}

void NetContext::run_worker(std::stop_token tkn, int efd)  {
    std::stop_callback __(tkn, [&]{
        eventfd_write(efd, 1);
    });

    {
        epoll_event ev;
        ev.data.ptr = nullptr;
        ev.events = EPOLLIN|EPOLLONESHOT;
        epoll_ctl(_epollfd, EPOLL_CTL_ADD, efd, &ev);
    }

    while (!tkn.stop_requested()) {
        epoll_event events[events_per_wait];
        int timeout = -1;
        bool timeout_thread = false;
        if (_cur_timer_thread.compare_exchange_strong(timeout, efd)) {
            timeout = get_epoll_timeout();
            timeout_thread= true;
        }
        int count = epoll_wait(_epollfd, events, events_per_wait, timeout);
        if (count == -1) {
            int e = errno;
            if (e != EINTR) {
                throw std::system_error(e, std::system_category(), "epoll_wait failed");
            }
            continue;
        }
        if (timeout_thread) {
            _cur_timer_thread = -1;
        }
        if (count == 0) {
            std::lock_guard _(_tmx);
            auto now = std::chrono::system_clock::now();
            while (!_pqueue.empty() && _pqueue.front().get_tp() <= now) {
                auto p = _pqueue.front().get_peer();
                if (_pqueue.size() > 1)
                    std::swap(_pqueue.front(), _pqueue.back());
                _pqueue.pop_back();
                p->on_timeout();
                if (tkn.stop_requested()) break;
            }
        } else for (int i = 0; i < count; ++i) {
            epoll_event &e = events[i];
            if (e.data.ptr != nullptr) {
                process_event(e);
            } else {
                eventfd_t dummy;
                eventfd_read(efd, &dummy);
            }
            if (tkn.stop_requested()) break;
        }
    }

}

void NetContext::receive(std::span<char> buffer, IPeer *peer) {
    int r;
    {
        auto ctx = peer->get_context_aux();
        std::lock_guard _(ctx->mx);
        r = ::recv(ctx->sock, buffer.data(), buffer.size(), MSG_DONTWAIT);
        if (r < 0) {
            int e = errno;
            if (e == EWOULDBLOCK) {
                ctx->flags |= EPOLLIN;
                epoll_event ev;
                ev.data.ptr = static_cast<IPeerServerCommon *>(peer);
                ev.events = ctx->flags | EPOLLONESHOT;
                epoll_ctl(_epollfd, EPOLL_CTL_MOD, ctx->sock, &ev);
                return;
            } else if (e == EPIPE || e == ECONNRESET ) {
                r = 0;
            } else {
                throw std::system_error(e, std::system_category(), "recv failed");
            }
        }
    }
    peer->on_read_complete(std::string_view(buffer.data(), r));
}

std::size_t NetContext::send(std::string_view data, IPeer *peer) {
    auto ctx = peer->get_context_aux();
    std::lock_guard _(ctx->mx);
    int s = ::send(ctx->sock, data.data(), data.size(), MSG_DONTWAIT);
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

void NetContext::callback_on_send_available( IPeer *peer) {
    auto ctx = peer->get_context_aux();
    std::lock_guard _(ctx->mx);
    ctx->flags |= EPOLLOUT;
    epoll_event ev;
    ev.data.ptr = static_cast<IPeerServerCommon *>(peer);
    ev.events = ctx->flags | EPOLLONESHOT;
    epoll_ctl(_epollfd, EPOLL_CTL_MOD, ctx->sock, &ev);
}

NetContextAux* NetContext::create_server(std::string address_port) {
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

    epoll_event ev;
    ev.data.ptr = nullptr;
    ev.events = EPOLLIN | EPOLLONESHOT;
    epoll_ctl(_epollfd, EPOLL_CTL_ADD, listen_fd, &ev);
    return new NetContextAux(listen_fd);

}

void NetContext::accept(IServer *server) {
    auto ctx = server->get_context_aux();
    std::lock_guard _(ctx->mx);
    ctx->flags |= EPOLLIN;
    ctx->server = true;
    epoll_event ev;
    ev.data.ptr = static_cast<IPeerServerCommon *>(server);
    ev.events = ctx->flags | EPOLLONESHOT;
    epoll_ctl(_epollfd, EPOLL_CTL_MOD, ctx->sock, &ev);
}

void NetContext::destroy(IPeerServerCommon *p) {
    auto ctx = p->get_context_aux();
    std::lock_guard _(ctx->mx);
    epoll_event ev = {};
    epoll_ctl(_epollfd, EPOLL_CTL_DEL, ctx->sock, &ev);
    delete ctx;
    clear_timeout(p);
}



NetContextAux *NetContext::peer_connect(std::string address_port)  {
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

    epoll_event ev;
    ev.data.ptr = nullptr;
    ev.events = EPOLLOUT | EPOLLONESHOT;
    epoll_ctl(_epollfd, EPOLL_CTL_ADD, sockfd, &ev);
    return new NetContextAux(sockfd);

}

 void NetContext::reconnect(IPeer *peer, std::string address_port) {
     auto ctx = peer->get_context_aux();
     std::lock_guard _(ctx->mx);
     auto new_aux = peer_connect(address_port);
     std::swap(new_aux->sock, ctx->sock);
     ctx->flags = 0;
 }

NetContextAux::NetContextAux(Socket s):sock(s) {

}
NetContextAux::~NetContextAux() {
    ::close(sock);
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



NetContext::TimerInfo::~TimerInfo() {
    if (_peer) {
        auto aux = _peer->get_context_aux();
        aux->timeout_ptr = nullptr;
    }
}

NetContext::TimerInfo::TimerInfo(std::chrono::system_clock::time_point tp, IPeerServerCommon *p)
    :_peer(p), _tp(tp)
{
    auto aux = _peer->get_context_aux();
    aux->timeout_ptr = this;
}

NetContext::TimerInfo::TimerInfo(TimerInfo &&other)
    :_peer(other._peer),_tp(other._tp) {
        auto aux = _peer->get_context_aux();
        aux->timeout_ptr = this;
        other._peer = nullptr;

}

NetContext::TimerInfo& NetContext::TimerInfo::operator =(TimerInfo &&other) {
    if (this != &other) {
        std::destroy_at(this);
        std::construct_at(this, std::move(other));
    }
    return *this;
}

std::chrono::system_clock::time_point NetContext::TimerInfo::get_tp() const {
    return _tp;
}



IPeerServerCommon* NetContext::TimerInfo::get_peer() const {
    return _peer;
}

void NetContext::set_timeout(std::chrono::system_clock::time_point tp, IPeerServerCommon *p) {
    std::lock_guard _(_tmx);
    bool ntf = _pqueue.empty() || _pqueue.front().get_tp() > tp;
    auto aux = p->get_context_aux();
    if (aux->timeout_ptr) {
        std::size_t idx = reinterpret_cast<const TimerInfo *>(aux->timeout_ptr) - _pqueue.data();
        if (idx <_pqueue.size()) {
            _pqueue.push_back(TimerInfo(tp, p));
            heapify_remove(_pqueue, idx, TimerInfo::compare);
            _pqueue[idx].refresh_pos();
            return;
        }
    }
    auto idx = _pqueue.size();
    _pqueue.push_back(TimerInfo(tp, p));
    heapify_up(_pqueue, idx, &TimerInfo::compare);
    if (ntf && _cur_timer_thread >= 0) {
        eventfd_write(_cur_timer_thread, 1);
    }
}

void NetContext::clear_timeout(IPeerServerCommon *p) {
    std::lock_guard _(_tmx);
    auto aux = p->get_context_aux();
    if (aux->timeout_ptr) {
        std::size_t idx = reinterpret_cast<const TimerInfo *>(aux->timeout_ptr) - _pqueue.data();
        if (idx <_pqueue.size()) {
            heapify_remove(_pqueue, idx, TimerInfo::compare);
        }
    }

}

void NetContext::process_event(const epoll_event &e) {
    IPeerServerCommon *sp = reinterpret_cast<IPeer *>(e.data.ptr);
    auto *ctx = sp->get_context_aux();
    std::lock_guard _(ctx->mx);
    if ((ctx->flags &= ~e.events) != 0) {
        epoll_event ev;
        ev.data.ptr = sp;
        ev.events = ctx->flags | EPOLLONESHOT;
        epoll_ctl(_epollfd, EPOLL_CTL_MOD, ctx->sock, &ev);
    }
    if (e.events & EPOLLIN) {
        if (ctx->server) {
            sockaddr_storage saddr_stor;
            socklen_t slen = sizeof(saddr_stor);
            sockaddr *saddr = reinterpret_cast<sockaddr *>(&saddr_stor);

            auto srv = static_cast<IServer *>(sp);

            auto n = accept4(ctx->sock, saddr, &slen, SOCK_CLOEXEC|SOCK_NONBLOCK);
            if (n < 0) {
                srv->on_accept(nullptr, {});
            } else {
                srv->on_accept(new NetContextAux(n), sockaddr_to_string(saddr));
            }
        } else {
            auto p = static_cast<IPeer *>(sp);
            int r = recv(ctx->sock, ctx->buffer.data(), ctx->buffer.size(), MSG_DONTWAIT);
            if (r < 0) {
                int e = errno;
                if (e == EWOULDBLOCK) {
                    ctx->flags |= EPOLLIN;
                    epoll_event ev;
                    ev.data.ptr = sp;
                    ev.events = ctx->flags | EPOLLONESHOT;
                    epoll_ctl(_epollfd, EPOLL_CTL_MOD, ctx->sock, &ev);
                } else {
                    p->on_read_complete({});
                }
            } else {
                p->on_read_complete(std::string_view(ctx->buffer.data(), r));
            }
        }
    }
    if (e.events & EPOLLOUT) {
        auto p = static_cast<IPeer *>(sp);
        p->on_send_available();
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
