#include "network_linux.h"

#include <utility>

#include <arpa/inet.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <netdb.h>
#include <fcntl.h>
#include <map>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;


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


static void default_log_function(std::string_view, std::source_location ) {
    //empty
}

NetContext::NetContext(ErrorCallback ecb)
    :_ecb(std::move(ecb))
    ,_tmset(TimeoutSet::allocator_type(&_pool))
 {
}

NetContext::NetContext(): NetContext(&default_log_function) {}



std::chrono::system_clock::time_point NetContext::get_epoll_timeout_lk() {
    if (!_tmset.empty()) {
        return _tmset.begin()->first;
    }
    return std::chrono::system_clock::time_point::max();
}

NetContext::SocketInfo *NetContext::alloc_socket_lk() {
    while (_first_free_socket_ident >= _sockets.size()) {
       _sockets.push_back(std::make_unique<SocketInfo>());
       _sockets.back()->_ident = static_cast<ConnHandle>(_sockets.size());
   }
   SocketInfo *nfo = _sockets[_first_free_socket_ident].get();
   std::swap(nfo->_ident,_first_free_socket_ident);
   return nfo;
}

void NetContext::free_socket_lk(ConnHandle id) {
    SocketInfo *nfo = _sockets[id].get();
    std::destroy_at(nfo);
    std::construct_at(nfo);
    nfo->_ident = _first_free_socket_ident;
    _first_free_socket_ident = id;
}

NetContext::SocketInfo *NetContext::socket_by_ident(ConnHandle id) {
    if (id >= _sockets.size()) return nullptr;
    auto r = _sockets[id].get();
    return r->_ident == id?r:nullptr;
}


void NetContext::run_worker(std::stop_token tkn, int efd)  {
    std::unique_lock lk(_mx);
    std::stop_callback __(tkn, [&]{
        eventfd_write(efd, 1);
    });

    std::vector<std::function<void()> > actions;

    _epoll.add(efd, EPOLLIN, -1);

    while (!tkn.stop_requested()) {
        auto timeout = std::chrono::system_clock::time_point::max();
        int needfd = -1;
        bool timeout_thread = false;
        if (_cur_timer_thread.compare_exchange_strong(needfd, efd)) {
            timeout = get_epoll_timeout_lk();
            timeout_thread= true;
        }
        lk.unlock();
        auto res = _epoll.wait(timeout);
        lk.lock();
        if (timeout_thread) {
            _cur_timer_thread = -1;
        }
        if (!res) {
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
                }
            }
        } else {
            auto &e = *res;
            if (e.ident != static_cast<ConnHandle>(-1)) {
                process_event_lk(lk, e);
            } else {
                eventfd_t dummy;
                eventfd_read(efd, &dummy);
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

void NetContext::receive(ConnHandle ident, std::span<char> buffer, IPeer *peer) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    ctx->_flags |= EPOLLIN;
    ctx->_recv_buffer = buffer;
    ctx->_recv_cb = peer;
    apply_flags_lk(ctx);
}

std::size_t NetContext::send(ConnHandle ident, std::string_view data) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return 0;
    if (data.empty()) {
        if (ctx->_socket_is_pipe) {
            if (ctx->_socket >= 0) {
                _epoll.del(ctx->_socket);
                ::close(ctx->_socket);
                ctx->_socket = -1;
            }
        } else {
            ::shutdown(ctx->_socket, SHUT_WR);
        }
        return 0;
    } else {
        int s;
        if (ctx->_socket_is_pipe) {
            s = ::write(ctx->_socket, data.data(), data.size());
        } else {
            s = ::send(ctx->_socket, data.data(), data.size(), MSG_DONTWAIT);
        }
        if (s < 0) {
            int e = errno;
            s = 0;
            if (e != EWOULDBLOCK && e != EPIPE && e != ECONNRESET) {
                report_error(std::system_error(e, std::system_category()), "send");
            }
        }
        return s;
    }
}

void NetContext::ready_to_send(ConnHandle ident, IPeer *peer) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    ctx->_flags |= EPOLLOUT;
    ctx->_send_cb = peer;
    apply_flags_lk(ctx);
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

void NetContext::accept(ConnHandle ident, IServer *server) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    ctx->_flags |= EPOLLIN;
    ctx->_accept_cb = server;
    apply_flags_lk(ctx);
}

void NetContext::destroy(ConnHandle ident) {
    std::unique_lock lk(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    _cond.wait(lk, [&]{return ctx->_cb_call_cntr == 0;}); //wait for finishing all callbacks
    if (ctx->_socket>=0) {
        _epoll.del(ctx->_socket);
        ::close(ctx->_socket);
    }
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


ConnHandle NetContext::connect(std::string address_port)  {

    int sockfd = connect_peer(address_port);
    std::lock_guard _(_mx);
    SocketInfo *nfo = alloc_socket_lk();
    nfo->_socket = sockfd;
    _epoll.add(sockfd, 0, nfo->_ident);
    return nfo->_ident;
}

 void NetContext::reconnect(ConnHandle ident, std::string address_port) {
     std::lock_guard _(_mx);
     auto ctx = socket_by_ident(ident);
     if (!ctx) return;
     auto newfd = connect_peer(std::move(address_port));
     if (ctx->_socket >= 0) {
         _epoll.del(ctx->_socket);
         ::close(ctx->_socket);
     }
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
    int efd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
    try {
        run_worker(std::move(tkn), efd);
        ::close(efd);
    } catch(...) {
        ::close(efd);
        throw;
    }
}




void NetContext::set_timeout(ConnHandle ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) {
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

void NetContext::clear_timeout(ConnHandle ident) {
    std::lock_guard _(_mx);
    auto ctx = socket_by_ident(ident);
    if (!ctx) return;
    _tmset.erase({ctx->_tmtp, ident});

}

template<typename Fn>
void NetContext::SocketInfo::invoke_cb(std::unique_lock<std::mutex> &lk, std::condition_variable &cond, Fn &&fn) {
    ++_cb_call_cntr;
    lk.unlock();
    fn();
    lk.lock();
    if (--_cb_call_cntr == 0) {
        cond.notify_all();
    }
}

void NetContext::process_event_lk(std::unique_lock<std::mutex> &lk, const WaitRes &e) {
    auto ctx = socket_by_ident(e.ident);
    if (!ctx) return;
    ctx->_cur_flags = 0;
    if (e.events & EPOLLIN) {
        if (ctx->_accept_cb) {
            ctx->_flags &= ~EPOLLIN;
            auto srv = std::exchange(ctx->_accept_cb, nullptr);
            sockaddr_storage saddr_stor;
            socklen_t slen = sizeof(saddr_stor);
            sockaddr *saddr = reinterpret_cast<sockaddr *>(&saddr_stor);
            auto n = accept4(ctx->_socket, saddr, &slen, SOCK_CLOEXEC|SOCK_NONBLOCK);
            if (n >= 0) {
                SocketInfo *nfo = alloc_socket_lk();
                ctx = socket_by_ident(e.ident); //reallocation of socket list, we must find ctx again
                nfo->_socket = n;
                _epoll.add(n,0, nfo->_ident);;

                ctx->invoke_cb(lk, _cond, [&]{if (srv) srv->on_accept(nfo->_ident, sockaddr_to_string(saddr));});
            } else {
                report_error(std::system_error(errno, std::system_category()), "accept");
            }
        }
        if (ctx->_recv_cb) {
            int r;
            if (ctx->_socket_is_pipe) {
                r = ::read(ctx->_socket, ctx->_recv_buffer.data(), ctx->_recv_buffer.size());
            } else {
                r = ::recv(ctx->_socket, ctx->_recv_buffer.data(), ctx->_recv_buffer.size(), MSG_DONTWAIT);
            }
            if (r < 0) {
                report_error(std::system_error(errno, std::system_category()), "receive");
                 r = 0; //any error - close connection
            }
            auto peer = std::exchange(ctx->_recv_cb, nullptr);
            ctx->_flags &= ~EPOLLIN;
            ctx->invoke_cb(lk, _cond, [&]{peer->receive_complete(std::string_view(ctx->_recv_buffer.data(), r));});
        }
    }
    if (e.events & EPOLLOUT) {
        ctx->_flags &= ~EPOLLOUT;
        auto peer = std::exchange(ctx->_send_cb, nullptr);
        if (peer) ctx->invoke_cb(lk, _cond, [&]{peer->clear_to_send();});
    }
    apply_flags_lk(ctx);

}



void NetContext::enqueue(std::function<void()> fn) {
    std::lock_guard _(_mx);
    _actions.push_back(std::move(fn));
    if (_cur_timer_thread >= 0) {
        eventfd_write(_cur_timer_thread, 1);
    }
}

static int dup_fd(int fd) {
    int r =  fcntl(fd,F_DUPFD_CLOEXEC, 0);
    if (r < 0) throw std::system_error(errno, std::system_category());
    return r;
}

ConnHandle NetContext::connect(SpecialConnection type, const void *arg) {
    std::lock_guard _(_mx);
    SocketInfo *ctx =alloc_socket_lk();
    switch (type) {
        default:
        case SpecialConnection::null: break;
        case SpecialConnection::descriptor:
            ctx->_socket = dup_fd(*reinterpret_cast<const int *>(arg));
            ctx->_socket_is_pipe = true;
            break;
        case SpecialConnection::socket:
            ctx->_socket = dup_fd(*reinterpret_cast<const int *>(arg));
            break;
        case SpecialConnection::stdinput:
            ctx->_socket = dup_fd(0);
            ctx->_socket_is_pipe = true;
            break;
        case SpecialConnection::stdoutput:
            ctx->_socket = dup_fd(1);
            ctx->_socket_is_pipe = true;
            break;
        case SpecialConnection::stderror:
            ctx->_socket = dup_fd(2);
            ctx->_socket_is_pipe = true;
            break;
    }
    _epoll.add(ctx->_socket, 0, ctx->_ident);
    return ctx->_ident;
}

PipePair NetContext::create_pipe() {
    int fds[2];
    ConnHandle conhndl[2];
    int p = pipe2(fds,O_CLOEXEC|O_NONBLOCK);
    if (p < 0) throw std::system_error(errno, std::system_category());
    for (int i = 0; i < 2; ++i) {
        auto ctx = alloc_socket_lk();
        ctx->_socket = fds[i];
        ctx->_socket_is_pipe = true;
        _epoll.add(ctx->_socket, 0, ctx->_ident);
        conhndl[i] = ctx->_ident;
    }
    return {conhndl[0], conhndl[1]};
}

void NetContext::apply_flags_lk(SocketInfo *ctx) noexcept {
    if (ctx->_flags != ctx->_cur_flags) {
        _epoll.mod(ctx->_socket, ctx->_flags|EPOLLONESHOT, ctx->_ident);
        ctx->_cur_flags = ctx->_flags;
    }
}


std::shared_ptr<INetContext> make_network_context(int iothreads) {
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

template <typename E>
void NetContext::report_error(E exception, std::string_view action, std::source_location loc)
{
    try {
        throw exception;
    } catch (...){
        _ecb(action, loc);
    }
}



class ProcessMonitorPeer: public IPeer {
public:

    struct TermProcess {
        int pid;
        void operator()() {::kill(pid, SIGTERM);}
    };


    struct ProcessInfo {
        std::function<void(int)> _on_exit_action;
        std::stop_callback<TermProcess> _stop_cb;
    };


    struct InitProcessInfo {
        int _pid;
        std::function<void(int)> _on_exit_action;
        std::stop_token _tkn;
        operator ProcessInfo()  {
            return {
                std::move(_on_exit_action),
                std::stop_callback<TermProcess>(_tkn, TermProcess{_pid})
            };
        }
    };

    ProcessMonitorPeer(std::shared_ptr<INetContext> ctx)
        :_ctx(std::move(ctx)) {
        sigset_t ss;
        sigemptyset(&ss);
        sigaddset(&ss, SIGCHLD);
        sigprocmask(SIG_BLOCK, &ss, NULL);
        int fd = signalfd(-1, &ss, SFD_CLOEXEC|SFD_NONBLOCK);
        if (fd == -1) std::system_error(errno, std::system_category());
        _sigfd = _ctx->connect(SpecialConnection::descriptor, &fd);
        ::close(fd);
        _ctx->receive(_sigfd, {reinterpret_cast<char *>(&_buffer),sizeof(_buffer)}, this);
    }

    ~ProcessMonitorPeer() {
        _ctx->destroy(_sigfd);
    }

    virtual void receive_complete(std::string_view) noexcept override {
        std::shared_ptr<ProcessMonitorPeer> me;
        std::unique_lock lk(_mx);
        int status;
        _fns.clear();
        auto iter = _pmap.begin();
        while (iter != _pmap.end()) {
            if (waitpid(iter->first, &status, WNOHANG) > 0) {
                if (iter->second._on_exit_action) {
                    _fns.emplace_back(std::move(iter->second._on_exit_action), status);
                }
                iter = _pmap.erase(iter);
            } else {
                ++iter;
            }
        }
        lk.unlock();
        for (auto &f: _fns) {
            f.first(f.second);
        }
        _fns.clear();
        _ctx->receive(_sigfd, {reinterpret_cast<char *>(&_buffer),sizeof(_buffer)}, this);
        if (_pmap.empty()) {
            me = std::move(_me);
        }
    }
    virtual void clear_to_send() noexcept override {}
    virtual void on_timeout() noexcept override {}

    static void spawn(
            std::shared_ptr<ProcessMonitorPeer> me,
            std::stop_token tkn,
            std::function<void(int)> exit_action,
            const char* program,
            char* const argv[],
            int read_fd,
            int write_fd) {

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, read_fd, STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, write_fd, STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, read_fd);
        posix_spawn_file_actions_addclose(&actions, write_fd);

        std::lock_guard _(me->_mx);

        pid_t pid;
        int status = posix_spawn(&pid, program, &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);

        if (status != 0) {
            throw std::system_error(status, std::system_category());
        }

        me->_pmap.emplace(pid, InitProcessInfo{pid, std::move(exit_action), std::move(tkn)});
        me->_me = me;
    }

protected:

    std::mutex _mx;
    std::shared_ptr<INetContext> _ctx;
    std::shared_ptr<ProcessMonitorPeer> _me;
    ConnHandle _sigfd;
    signalfd_siginfo _buffer;
    std::unordered_map<int, ProcessInfo> _pmap;
    std::vector<std::pair<std::function<void(int)>, int > > _fns;
};



static std::mutex pmonpeer_mx;
static std::weak_ptr<ProcessMonitorPeer> pmonpeer;

std::pair<std::vector<char *>, std::vector<char> > parse_command_line(std::string_view cmdline) {
    std::vector<char *> pointers;
    std::vector<char> data;
    data.resize(cmdline.size()+2);
    char *iter = &data[0];
    bool add_arg = true;
    bool spec = false;
    bool dbl = false;
    bool esc = false;

    for (auto c: cmdline) {
        if (esc) {
            *iter = c;
            ++iter;
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (spec) {
            if (c == '"') {
                spec = false;
                dbl = true;
            } else {
                *iter = c;
                ++iter;
            }
        } else {
            if (isspace(c)) {
                add_arg = true;dbl = false;
                continue;
            }
            if (add_arg) {
                *iter = '\0';
                ++iter;
                pointers.push_back(iter);
                add_arg = false;
                dbl = false;
            }
            if (c == '"') {
                spec = true;
                if (dbl) {
                    *iter = '"';
                    ++iter;
                }
            } else {
                *iter = c;
                ++iter;
                dbl = false;
            }
        }
    }
    *iter = '\0';
    pointers.push_back(nullptr);
    return {
        std::move(pointers),
        std::move(data)
    };

}


PipePair spawn_process(std::shared_ptr<INetContext> ctx,
                        std::string_view command_line,
                        std::stop_token tkn ,
                        std::function<void(int)> exit_action) {

    std::lock_guard _(pmonpeer_mx);
    auto pmon = pmonpeer.lock();
    if (!pmon) {
        pmon = std::make_shared<ProcessMonitorPeer>(ctx);
        pmonpeer = pmon;
    }

    int p1[2];
    int p2[2];
    int r = pipe2(p1, O_CLOEXEC| O_NONBLOCK);
    if (r < 0) {
        throw std::system_error(errno, std::system_category());
    }
    r = pipe2(p2, O_CLOEXEC| O_NONBLOCK);
    if (r < 0) {
        int e = errno;
        close(p1[0]);
        close(p1[1]);
        throw std::system_error(e, std::system_category());
    }

    auto re = ctx->connect(SpecialConnection::descriptor, p1);
    auto we = ctx->connect(SpecialConnection::descriptor, p2+1);
    close(p1[0]);
    close(p2[1]);
    auto cmdline = parse_command_line(command_line);
    try {
        ProcessMonitorPeer::spawn(pmon, std::move(tkn), std::move(exit_action),
                cmdline.first[0], cmdline.first.data(),
                p2[0], p1[1]);
    } catch (...) {
        close(p2[0]);
        close(p1[1]);
        throw;
    }
    close(p2[0]);
    close(p1[1]);
    return {re,we};




}



}

