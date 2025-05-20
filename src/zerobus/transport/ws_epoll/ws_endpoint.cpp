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

    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            continue;

        int r = connect(sockfd, rp->ai_addr, rp->ai_addrlen);
        if (r != -1 ) {
            // Úspěšné připojení
            freeaddrinfo(result);
            return sockfd;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);
    throw std::system_error(errno, std::generic_category(), "Connect failed");
}
void WsEndpoint::SocketDeleter::operator ()(int int1) {
    ::close(int1);
}


void WsEndpoint::close_conn(std::shared_ptr<Context> ctx, Handle h) {
    _epoll.del(ctx->_sock.get());
    if (ctx->_reconnect_addr.empty()) {
        _handles.erase(h);
    } else {
        ctx->_connecting = true;
        ctx->_input_buffer.clear();
        ctx->_ws_parser.reset();
        delay_connect(ctx, h);
    }

}

bool WsEndpoint::process_read_event(Handle h) {
    std::lock_guard _(_mx);
    if (h == _listen_socket_handle) {
        int newsock = accept4(_listen_socket.get(), nullptr, nullptr,
                SOCK_CLOEXEC);
        if (newsock != -1) {
            auto ctx = std::make_shared<Context>(newsock);
            h = _handles.emplace(std::move(ctx));
            _epoll.add(newsock, EPOLLIN | EPOLLONESHOT, h);
        }
    } else {
        auto iter = _handles.find(h);
        if (iter != _handles.end()) {
            std::shared_ptr<Context> ctx = iter->_value;
            int sock = ctx->_sock.get();
            if (ctx->_connecting) {
                _epoll.del(ctx->_sock.get());
                ctx->_sock.reset();
                try {
                    ctx->_sock.reset(connectToAddress(ctx->_reconnect_addr));
                } catch (...) {
                    delay_connect(ctx, h);
                }
            } else {
                Res r = process_read_data(ctx);
                switch (r) {
                    case Res::error:
                        close_conn(ctx,h);
                        break;
                    case Res::event:
                        return true;
                    default:
                    case Res::ok:
                        _epoll.mod(sock, EPOLLIN | EPOLLONESHOT, h);
                        break;
                }
            }
        }
    }
    return false;
}

WsEndpoint::RecStatus WsEndpoint::receive(Message &msg, std::chrono::system_clock::time_point timeout) {
    Handle curh = msg.prev_handle;
    msg.prev_handle = 0;
    while (true) {
        if (curh) {
            std::lock_guard _(_mx);
            auto iter = _handles.find(curh);
            if (iter != _handles.end()) {
                auto ctx = iter->_value;
                while (ctx->_ws_parser.reset_parse_next()) {
                    auto wsmsg = ctx->_ws_parser.get_message();
                    Res res = process_ws_message(ctx, wsmsg);
                    msg.ident = ctx->_ident;
                    msg.prev_handle = curh;
                    if (res == Res::event) {
                        msg.data = wsmsg.payload;
                        return RecStatus::message;
                    } else if (res == Res::pong) {
                        msg.data = {};
                        return RecStatus::pong;
                    } else if (res == Res::error) {
                        close_conn(ctx, curh);
                        break;
                    }
                }
                _epoll.mod(ctx->_sock.get(), EPOLLIN|EPOLLONESHOT, curh);
            }
        }
        while (true) {
            auto wr =_epoll.wait(timeout);
            if (!wr) {
                return RecStatus::timeout;
            } else if (wr->ident) {
                Handle h = wr->ident;
                if (process_read_event(h)) {
                    curh = h;
                    break;
                }
            } else {
                _wakeup.read_and_clear();
                return RecStatus::interrupt;
            }

        }
    }
}

WsEndpoint::WsEndpoint() {
    _epoll.add(_wakeup.get_fd(), EPOLLIN, 0);
}

void WsEndpoint::delay_connect(const std::shared_ptr<Context> &ctx, Handle h) {
    int fd = timerfd_create(CLOCK_MONOTONIC, O_CLOEXEC);
    ctx->_sock.reset(fd);
    struct itimerspec ts = {};
    ts.it_value = {5,0 };
    timerfd_settime(fd, 0, &ts, NULL);
    _epoll.add(ctx->_sock.get(), EPOLLIN | EPOLLONESHOT, h);
}


WsEndpoint::Res WsEndpoint::process_read_data(const std::shared_ptr<Context> &c) {
    char buff[1500];
    Context &ctx = *c;
    int r = ::recv(ctx._sock.get(), buff, sizeof(buff), MSG_DONTWAIT);
    if (r < 0) {
        int e = errno;
        if (e == EWOULDBLOCK) return Res::ok;
        return Res::error;
    } else if (r == 0) {
        return Res::error;
    } else {
        bool ok = false;
        auto data = std::string_view(buff,r);
        if (ctx._awaiting_header) {
            auto extra = process_http_header(c, data);
            if (!extra) return Res::error;
            if (extra->empty()) {
                ok = ctx._ws_parser.push_data(*extra);
            }
        } else  {
            ok = ctx._ws_parser.push_data(data);
        }
        return ok?Res::event:Res::ok;
    }

}

WsEndpoint::Res WsEndpoint::process_ws_message(const std::shared_ptr<Context> &ctx, ws::Message &msg) {
    switch (msg.type) {
        case ws::Type::binary:
            return Res::event;
        case ws::Type::connClose:
            send_msg(ctx, ws::Message("",ws::Type::connClose));
            return Res::error;
        case ws::Type::ping:
            send_msg(ctx, ws::Message(msg.payload,ws::Type::pong));
            return Res::ok;
        case ws::Type::pong:
            return Res::pong;
        default:
            return Res::ok;
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

bool WsEndpoint::send_message(std::string_view data,  Handle slot) {
    std::shared_ptr<Context> ctx;
    {
        std::lock_guard _(_mx);
        auto iter = _handles.find(slot);
        if (iter == _handles.end()) return false;
        ctx = iter->_value;
    }
    return send_msg(ctx, ws::Message(data, ws::Type::binary));
}

bool WsEndpoint::send_msg(const std::shared_ptr<Context> &ctx, ws::Message msg) {
    return utils::stack_alloc<char>(msg.payload.size()+16, [&](char *from){
        char *to = from;
        ws::build(msg, [&](char c) {*to++=c;});
        std::lock_guard _(ctx->_send_mx);
        if (ctx->_connecting) return false;
        return send_buffer(ctx->_sock.get(), std::string_view(from, to));
    });
}

void WsEndpoint::bind(const std::string &address_port) {
    int sock = create_listening_socket(address_port);
    std::lock_guard _(_mx);
    _listen_socket.reset(sock);
    _listen_socket_handle = _handles.emplace(std::shared_ptr<Context>());

}

void WsEndpoint::connect(const std::string &address_port) {
    auto ctx = std::make_shared<Context>(-1);
    ctx->_reconnect_addr = address_port;
    generate_mailbox_id(std::back_inserter(ctx->_ident));
    ctx->_sock.reset(connectToAddress(address_port));
    ctx->_connecting = true;
    if (!send_ws_request(ctx)) {
        throw std::system_error(errno, std::system_category(), "Failed to send initial handshake");
    }
    std::lock_guard _(_mx);
    auto h = _handles.emplace(ctx);
    _epoll.add(ctx->_sock.get(), EPOLLIN|EPOLLONESHOT, h);
}

std::optional<std::string_view> WsEndpoint::process_http_header(
        const std::shared_ptr<Context> &ctx, std::string_view data) {

    auto &buff = ctx->_input_buffer;
    buff.insert(buff.end(), data.begin(), data.end());
    std::string_view hdr_data(buff.begin(), buff.end());
    std::string_view extra;
    auto pos = hdr_data.find("\r\n\r\n");
    if (pos == hdr_data.npos) return {std::string_view()};

    extra = hdr_data.substr(pos);
    hdr_data = hdr_data.substr(0, pos-2);


    bool upgrade = false;
    bool conn_upgrade = false;
    std::string_view sock_key;
    bool version = false;
    auto fline = split_at(hdr_data,"\r\n");


    if (ctx->_connecting) {
        auto proto = HeaderKey(split_at(fline, " "));
        auto code = split_at(fline, " ");

        if (code != "101" || proto != "HTTP/1.1") return std::nullopt;

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
            else if (key == "Sec-WebSocket-Accept") {
                sock_key = val;
            }
        }

        if (!upgrade || !conn_upgrade || sock_key != ctx->_ws_accept) return std::nullopt;
        ctx->_ws_accept.clear();
        ctx->_connecting = false;

    } else {
        auto fline = split_at(hdr_data,"\r\n");
        auto method = HeaderKey(split_at(fline, " "));
        auto ident = split_at(fline, " ");

        if (method != "GET") return std::nullopt;

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
    }
    ctx->_ws_parser.reset();
    ctx->_awaiting_header = false;
    return extra;

}

bool WsEndpoint::send_ws_request(const std::shared_ptr<Context> &ctx) {
    auto key = ws::generate_ws_key();
    ctx->_ws_accept = ws::calculate_ws_accept(key);

    std::ostringstream req;
    req << "GET " << ctx->_ident << " HTTP/1.1\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Host: " << ctx->_reconnect_addr << "\r\n"
           "User-Agent: zerobus/1.0\r\n"
           "Sec-WebSocket-Key: " << static_cast<std::string_view>(key) << "\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "\r\n";

    return send_buffer(ctx->_sock.get(), req.view());

}

void WsEndpoint::set_interrupt() {
    _wakeup.set();
}

}
