#include "ws_endpoint.hpp"
#include "../../utils/stack_alloc.hpp"
#include "../utils/http_utils.hpp"
#include "../../utils/random_channel_gen.hpp"

#include <fcntl.h>

#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <netdb.h>
namespace zerobus {


int create_listening_socket(const std::string& address_port) {
    std::string host;
    std::string port;

    size_t colon_pos = address_port.rfind(':');
    if (colon_pos == std::string::npos || colon_pos == address_port.length() - 1) {
        throw std::invalid_argument("Invalid address format. Expected format: [host]:port or :port");
    }

    host = address_port.substr(0, colon_pos);
    port = address_port.substr(colon_pos + 1);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;        // IPv4 nebo IPv6
    hints.ai_socktype = SOCK_STREAM;    // TCP
    hints.ai_flags = AI_PASSIVE;        // Pro listen socket

    addrinfo* result;
    int ret = getaddrinfo(host.empty() ? nullptr : host.c_str(), port.c_str(), &hints, &result);
    if (ret != 0) {
        throw std::system_error(0, std::generic_category(), std::string("getaddrinfo: ") + gai_strerror(ret));
    }

    int sockfd = -1;
    for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) continue;

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            if (listen(sockfd, SOMAXCONN) == 0) {
                freeaddrinfo(result);
                return sockfd;
            }
        }

        // jinak zavřít a zkusit další
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);
    throw std::system_error(errno, std::generic_category(), "Failed to bind and listen on any address");
}



int connectToAddress(const std::string& addressPort) {
    std::string host;
    std::string port;

    // Rozdělení na adresu a port
    auto colonPos = addressPort.rfind(':');
    if (colonPos == std::string::npos) {
        throw std::invalid_argument("Invalid address format. Expected format: host:port");
    }
    host = addressPort.substr(0, colonPos);
    port = addressPort.substr(colonPos + 1);

    struct addrinfo hints{};
    struct addrinfo* result;
    int sockfd = -1;

    hints.ai_family = AF_UNSPEC;        // IPv4 nebo IPv6
    hints.ai_socktype = SOCK_STREAM;    // TCP
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (s != 0) {
        throw std::system_error(errno, std::generic_category(), "getaddrinfo failed: " + std::string(gai_strerror(s)));
    }

    int e = ENOENT;
    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            continue;
        fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

        int r = connect(sockfd, rp->ai_addr, rp->ai_addrlen);
        e = errno;
        if (r != -1 ||  e == EINPROGRESS ) {
            // Úspěšné připojení
            freeaddrinfo(result);
            return sockfd;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);
    throw std::system_error(e, std::generic_category(), "Connect failed");
}
void AbstractWsEndpoint::SocketDeleter::operator ()(int int1) {
    ::close(int1);
}

AbstractWsEndpoint::AbstractWsEndpoint(
        unsigned int hk_sec, unsigned int threads)
:_hk_sec(hk_sec) {
    _next_hk = std::chrono::system_clock::now() + std::chrono::seconds(hk_sec);
    _epoll.add(_wakeup.get_fd(), EPOLLIN, 0);
    _pool.add_threads(threads, [this](std::stop_token stp, const bool &kf){
        worker(stp, kf);
    });
}

void AbstractWsEndpoint::delay_connect(const std::shared_ptr<Context> &ctx, Handle h) {
    int fd = timerfd_create(CLOCK_MONOTONIC, O_CLOEXEC);
    ctx->_sock.reset(fd);
    struct itimerspec ts = {};
    ts.it_value = {5,0 };
    timerfd_settime(fd, 0, &ts, NULL);
    _epoll.add(ctx->_sock.get(), EPOLLIN | EPOLLONESHOT, h);
}

void AbstractWsEndpoint::worker(std::stop_token stp, const bool &kf) {
    auto tm = _next_hk.exchange(std::chrono::system_clock::time_point::max());
    std::stop_callback _(stp, [this]{
        _wakeup.set();
    });

    while(!stp.stop_requested()) {
        auto wr =_epoll.wait(tm);
        if (!wr) {
            tm = std::chrono::system_clock::now() + std::chrono::seconds(_hk_sec);
            on_housekeeping();
            if (kf) return;
        } else {
            std::unique_lock lk(_mx);
            if (wr->ident == 0) break;
            else if (wr->ident == _listen_socket_handle) {
                int newsock = accept4(_listen_socket.get(),nullptr, nullptr, SOCK_CLOEXEC);
                if (newsock != -1) {
                    auto ctx = std::make_shared<Context>(newsock);
                    auto h = _handles.emplace(std::move(ctx));
                    _epoll.add(newsock, EPOLLIN|EPOLLONESHOT, h);
                }
            } else {
                auto iter = _handles.find(wr->ident);
                if (iter != _handles.end()) {
                    std::shared_ptr<Context> ctx = iter->_value;
                    int sock = ctx->_sock.get();
                    lk.unlock();
                    if (ctx->_connecting) {

                        if (wr->events & EPOLLIN) {
                            //timer;
                            _epoll.del(ctx->_sock.get());
                            ctx->_sock.reset();
                            try {
                                ctx->_sock.reset(connectToAddress(ctx->_reconnect_addr));
                                _epoll.add(ctx->_sock.get(), EPOLLOUT|EPOLLONESHOT, wr->ident);
                            } catch (...) {
                                delay_connect(ctx, wr->ident);
                            }
                        } else if (wr->events & EPOLLOUT) {
                            int err = -1;
                            socklen_t len = sizeof(err);
                            getsockopt(ctx->_sock.get(), SOL_SOCKET, SO_ERROR, &err, &len);
                            if (err) {
                                delay_connect(ctx, wr->ident);
                            } else {
                                {
                                std::lock_guard _(ctx->_send_mx);
                                ctx->_connecting = false;
                                _epoll.mod(ctx->_sock.get(), EPOLLIN|EPOLLONESHOT, wr->ident);
                                }
                                ctx->_awaiting_header = true;
//                                send_ws_request(ctx);
                            }
                        }


                    } else {
                        bool c =  process_read_data(ctx,  wr->ident);
                        if (kf) return;
                        lk.lock();
                        if (c) {
                            _epoll.mod(sock, EPOLLIN|EPOLLONESHOT, wr->ident);
                        } else {
                            _epoll.del(sock);
                            std::lock_guard _(ctx->_send_mx);
                            if (ctx->_reconnect_addr.empty()) {
                                _handles.erase(wr->ident);;
                            } else {
                                ctx->_connecting = true;
                                delay_connect(ctx, wr->ident);
                            }
                        }
                    }
                }
            }

        }

    }

}

bool AbstractWsEndpoint::process_read_data(const std::shared_ptr<Context> &c,  Handle h) {
    char buff[1500];
    Context &ctx = *c;
    int r = ::recv(ctx._sock.get(), buff, sizeof(buff), MSG_DONTWAIT);
    if (r < 0) {
        int e = errno;
        if (e == EWOULDBLOCK) return true;
        return false;
    } else if (r == 0) {
        return false;
    } else {
        bool ok = false;
        auto data = std::string_view(buff,r);
        if (ctx._awaiting_header) {
            auto extra = process_http_header(c, data);
            if (!extra) return false;
            if (extra->empty()) {
                ok = ctx._ws_parser.push_data(*extra);
            }
        } else  {
            ok = ctx._ws_parser.push_data(data);
        }
        while (ok) {
            bool cont = process_ws_message(c, ctx._ws_parser.get_message(),h);
            if (!cont) {
                return false;
            }
            ok = ctx._ws_parser.reset_parse_next();
        }
    }
    return true;
}

bool AbstractWsEndpoint::process_ws_message(const std::shared_ptr<Context> &c, ws::Message msg, Handle h) {
    switch (msg.type) {
        case ws::Type::binary:
            on_incoming_message(msg.payload, c->_ident, h);
            return true;
        case ws::Type::connClose:
            send_msg(c, ws::Message("",ws::Type::connClose));
            return false;
        case ws::Type::ping:
            on_incoming_message("", c->_ident, h);
            send_msg(c, ws::Message(msg.payload,ws::Type::pong));
            return true;
        default:
            return true;
    }
}

static bool send_buffer(int sock, std::string_view data) {
    while (!data.empty()) {
        int r = ::send(sock, data.data(), data.size(), 0);
        if (r <= 0) {
            int e = errno;
            if (e == EINTR) continue;
            return false;
        }
        data = data.substr(r);
    }
    return true;
}

bool AbstractWsEndpoint::send_message(std::string_view data,  Handle slot) {
    std::shared_ptr<Context> ctx;
    {
        std::lock_guard _(_mx);
        auto iter = _handles.find(slot);
        if (iter == _handles.end()) return false;
        ctx = iter->_value;
    }
    return send_msg(ctx, ws::Message(data, ws::Type::binary));
}

bool AbstractWsEndpoint::send_msg(const std::shared_ptr<Context> &ctx, ws::Message msg) {
    return utils::stack_alloc<char>(msg.payload.size()+16, [&](char *from){
        char *to = from;
        ws::build(msg, [&](char c) {*to++=c;});
        std::lock_guard _(ctx->_send_mx);
        if (ctx->_connecting) return false;
        return send_buffer(ctx->_sock.get(), std::string_view(from, to));
    });
}

void AbstractWsEndpoint::bind(const std::string &address_port) {
    int sock = create_listening_socket(address_port);
    std::lock_guard _(_mx);
    _listen_socket.reset(sock);
    _listen_socket_handle = _handles.emplace(std::shared_ptr<Context>());

}

void AbstractWsEndpoint::connect(const std::string &address_port) {
    auto ctx = std::make_shared<Context>(-1);
    ctx->_reconnect_addr = address_port;
    generate_mailbox_id(std::back_inserter(ctx->_ident));
    ctx->_sock.reset(connectToAddress(address_port));
    ctx->_connecting = true;
    std::lock_guard _(_mx);
    auto h = _handles.emplace(ctx);
    _epoll.add(ctx->_sock.get(), EPOLLOUT|EPOLLONESHOT, h);
}

std::optional<std::string_view> AbstractWsEndpoint::process_http_header(
        const std::shared_ptr<Context> &ctx, std::string_view data) {

    auto &buff = ctx->_input_buffer;
    buff.insert(buff.end(), data.begin(), data.end());
    std::string_view hdr_data(buff.begin(), buff.end());
    std::string_view extra;
    auto pos = hdr_data.find("\r\n\r\n");
    if (pos == hdr_data.npos) return {std::string_view()};

    extra = hdr_data.substr(pos);
    hdr_data = hdr_data.substr(0, pos-2);

    auto fline = split_at(hdr_data,"\r\n");
    auto method = split_at(fline, " ");
    auto ident = split_at(fline, " ");

    bool upgrade = false;
    bool conn_upgrade = false;
    std::string_view sock_key;
    bool version = false;

    if (HeaderKey(method) != "GET") return std::nullopt;

    while (!hdr_data.empty()) {
        auto val = split_at(fline, "\r\n");
        HeaderKey key = trim(split_at(val, ":"));
        val = trim(val);

        if (key == "Connection") {
            if (HeaderKey(val) != "Upgrade") return std::nullopt;
            conn_upgrade = true;
        }
        else if (key == "Upgrade") {
            if (HeaderKey(val) != "websocket") return std::nullopt;
            upgrade = true;
        }
        else if (key == "Sec-WebSocket-Key") {
            sock_key = val;
        }
        else if (key == "Sec-WebSocket-Version") {
            auto  ver = std::strtoul(key.data(), nullptr, 10);
            if (ver < 13) return std::nullopt;
        }
    }
    if (!upgrade || !conn_upgrade || sock_key.empty() || !version) return std::nullopt;

    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Server: zerobus/1.0\r\n"
            "Sec-WebSocket-Accept: "
            << static_cast<std::string_view>(
                    ws::calculate_ws_accept(sock_key)) << "\r\n";

    std::lock_guard _(ctx->_send_mx);
    if (!send_buffer(ctx->_sock.get(), resp.view()))  {
        return std::nullopt;
    }
    ctx->_ident=ident;
    ctx->_ws_parser.reset();
    ctx->_awaiting_header = false;
    return extra;





}

}
