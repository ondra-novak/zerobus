#include "ws_bridge.hpp"
#include "../../utils/stack_alloc.hpp"
#include "../utils/http_utils.hpp"
#include "../../utils/random_channel_gen.hpp"
#include "webserver.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <netdb.h>
#include <string>
namespace zerobus
{

    int create_listening_socket(const std::string &address_port)
    {
        std::string host;
        std::string port;

        size_t colon_pos = address_port.rfind(':');
        if (colon_pos == std::string::npos || colon_pos == address_port.length() - 1)
        {
            throw std::invalid_argument("Invalid address format. Expected format: [host]:port or :port");
        }

        host = address_port.substr(0, colon_pos);
        port = address_port.substr(colon_pos + 1);

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;     // IPv4 nebo IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP
        hints.ai_flags = AI_PASSIVE;     // Pro listen socket

        addrinfo *result;
        int ret = getaddrinfo(host.empty() ? nullptr : host.c_str(), port.c_str(), &hints, &result);
        if (ret != 0)
        {
            throw std::system_error(0, std::generic_category(), std::string("getaddrinfo: ") + gai_strerror(ret));
        }

        int sockfd = -1;
        for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next)
        {
            sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sockfd == -1)
                continue;

            int opt = 1;
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            {
                if (listen(sockfd, SOMAXCONN) == 0)
                {
                    freeaddrinfo(result);
                    return sockfd;
                }
            }

            // jinak zavřít a zkusit další
            ::close(sockfd);
            sockfd = -1;
        }

        freeaddrinfo(result);
        throw std::system_error(errno, std::generic_category(), "Failed to bind and listen on any address");
    }

    int connectToAddress(const std::string &addressPort)
    {
        std::string host;
        std::string port;

        // Rozdělení na adresu a port
        auto colonPos = addressPort.rfind(':');
        if (colonPos == std::string::npos)
        {
            throw std::invalid_argument("Invalid address format. Expected format: host:port");
        }
        host = addressPort.substr(0, colonPos);
        port = addressPort.substr(colonPos + 1);

        struct addrinfo hints{};
        struct addrinfo *result;
        int sockfd = -1;

        hints.ai_family = AF_UNSPEC;     // IPv4 nebo IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP
        hints.ai_flags = 0;
        hints.ai_protocol = 0;

        int s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
        if (s != 0)
        {
            throw std::system_error(errno, std::generic_category(), "getaddrinfo failed: " + std::string(gai_strerror(s)));
        }

        for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next)
        {
            sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sockfd == -1)
                continue;

            int r = connect(sockfd, rp->ai_addr, rp->ai_addrlen);
            if (r != -1)
            {
                // Úspěšné připojení
                freeaddrinfo(result);
                return sockfd;
            }

            ::close(sockfd);
            sockfd = -1;
        }

        freeaddrinfo(result);
        throw std::system_error(errno, std::generic_category(), "Connect failed");
    }

    int create_reconnect_timer()
    {
        int fd = timerfd_create(CLOCK_MONOTONIC, O_CLOEXEC);
        struct itimerspec ts = {};
        ts.it_value = {5, 0};
        timerfd_settime(fd, 0, &ts, NULL);
        return fd;
    }

    void WsBridge::SocketDeleter::operator()(int int1)
    {
        ::close(int1);
    }

    bool WsBridge::Server::on_epoll_event(int)
    {
        int newsock = accept4(_sock.get(), nullptr, nullptr,
                              SOCK_CLOEXEC);
        if (newsock != -1)
        {
            _owner.create_peer(newsock);
        }
        return true;
    }

    WsBridge::Server::Server(WsBridge &owner, int socket)
        : _owner(owner), _sock(socket, {})
    {
    }

    WsBridge::Peer::Peer(WsBridge &owner)
        : Bridge(owner._bus, owner.create_transport(this, _bridge_parser)), _owner(owner), _ws_parser(_input_buffer, false) {}

    WsBridge::Peer::Peer(WsBridge &owner, int socket)
        : Peer(owner)
    {
        _sock.reset(socket);
        _mode = PeerOpMode::await_request;
    }
    WsBridge::Peer::Peer(WsBridge &owner, std::string address)
        : Peer(owner)
    {

        _mode = PeerOpMode::connecting;
        connect(address);
    }

    char *WsBridge::Peer::output_start(std::size_t sz)
    {
        _send_mx.lock();
        _output_buffer.clear();
        _output_buffer.resize(sz);
        return _output_buffer.data();
    }
    void WsBridge::Peer::output_commit(std::size_t sz)
    {
        _output_buffer.resize(sz);
        if (_mode == PeerOpMode::message)
        {
            _output_buffer.reserve(2 * _output_buffer.size() + 16);
            std::string_view data(_output_buffer.data(), sz);
            send_message(ws::Message(data, ws::Type::binary));
        }
        _send_mx.unlock();
    }

    void WsBridge::Peer::send_message(const ws::Message &msg)
    {
        uint8_t masking[4];
        uint8_t *masking_ptr = {};
        if (_mask_rnd)
        {
            masking_ptr = masking;
            std::uniform_int_distribution<int> dist(0x00, 0xFF);
            for (int i = 0; i < 4; ++i)
            {
                masking_ptr[i] = static_cast<std::uint8_t>(dist(*_mask_rnd));
            }
        }
        std::size_t needsz = msg.payload.size() + 16;
        std::size_t sz = _output_buffer.size();
        _output_buffer.resize(sz + needsz);
        std::size_t idx = sz;
        ws::build(msg, [&](char c)
                  { _output_buffer[idx++] = c; }, masking_ptr);
        send_buffer({_output_buffer.data() + sz, idx - sz});
    }

    bool WsBridge::Peer::conn_error()
    {
        if (_reconnect_addr.empty())
            return false;
        _mode = PeerOpMode::reconnect;
        this->disconnect();
        _sock.reset(create_reconnect_timer());
        return true;
    }
    bool WsBridge::Peer::on_epoll_event(int event)
    {
        if (event & EPOLLIN)
        {
            if (_mode == PeerOpMode::reconnect) [[unlikely]]
            {
                try
                {
                    connect(_reconnect_addr);
                    return true;
                }
                catch (...)
                {
                    return conn_error();
                }
            }
            char buff[1500];
            int r = ::recv(_sock.get(), buff, sizeof(buff), MSG_DONTWAIT);
            if (r < 0)
            {
                int e = errno;
                if (e == EWOULDBLOCK)
                    return true;
                std::lock_guard _(_send_mx);
                return conn_error();
            }
            else if (r == 0)
            {
                std::lock_guard _(_send_mx);
                return conn_error();
            }
            else
            {
                bool ok = false;
                auto data = std::string_view(buff, r);
                if (_mode != PeerOpMode::message)
                {
                    auto extra = read_http_header(data);
                    if (!extra)
                    {
                        std::lock_guard _(_send_mx);
                        return conn_error();
                    }
                    if (extra->empty())
                    {
                        ok = _ws_parser.push_data(*extra);
                    }
                }
                else
                {
                    ok = _ws_parser.push_data(data);
                }
                while (ok)
                {
                    auto msg = _ws_parser.get_message();
                    switch (msg.type)
                    {
                    case ws::Type::binary:
                        _bridge_parser->parse(msg.payload);
                        break;
                    case ws::Type::connClose:
                    {
                        std::lock_guard _(_send_mx);
                        send_message({"", ws::Type::connClose, ws::Base::closeNormal});
                        return conn_error();
                    }
                    case ws::Type::ping:
                    {
                        std::lock_guard _(_send_mx);
                        send_message({msg.payload, ws::Type::pong});
                        break;
                    }
                    default:
                        break;
                    }
                    ok = _ws_parser.reset_parse_next();
                }
            }
        }
        if (event & EPOLLOUT)
        {
            int error = 1;
            socklen_t len = sizeof(error);
            getsockopt(_sock.get(), SOL_SOCKET, SO_ERROR, &error, &len);
            if (error != 0 || !send_ws_request())
            {
                _mode = PeerOpMode::reconnect;
                _sock.reset(create_reconnect_timer());
            }
            else
            {
                _mode = PeerOpMode::await_response;
            }
        }
        return true;
    }

    void WsBridge::Peer::connect(std::string address)
    {
        std::lock_guard _(_send_mx);
        _mask_rnd.emplace(std::random_device()());
        _output_buffer.clear();
        _input_buffer.clear();
        _ws_parser.reset();
        _mode = PeerOpMode::connecting;
        _reconnect_addr = std::move(address);
        _sock.reset(connectToAddress(_reconnect_addr));
    }

    bool WsBridge::Peer::send_buffer(std::string_view data)
    {
        pollfd pfd;
        pfd.events = POLLOUT;
        pfd.fd = _sock.get();
        pfd.revents = 0;
        while (!data.empty())
        {
            pfd.revents = 0;
            if (poll(&pfd, 1, write_timeout) == 0)
                return false;
            int r = ::send(_sock.get(), data.data(), data.size(), 0);
            if (r <= 0)
            {
                return false;
            }
            data = data.substr(r);
        }
        return true;
    }

    bool WsBridge::Peer::send_ws_request()
    {
        auto key = ws::generate_ws_key();
        _ws_accept = ws::calculate_ws_accept(key);

        std::ostringstream req;
        req << "GET / HTTP/1.1\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Host: "
            << _reconnect_addr << "\r\n"
                                  "User-Agent: zerobus/1.0\r\n"
                                  "Sec-WebSocket-Key: "
            << static_cast<std::string_view>(key) << "\r\n"
                                                     "Sec-WebSocket-Version: 13\r\n"
                                                     "\r\n";

        return send_buffer(req.view());
    }

    std::optional<std::string_view> WsBridge::Peer::read_http_header(std::string_view data)
    {

        auto &buff = _input_buffer;
        buff.insert(buff.end(), data.begin(), data.end());
        std::string_view hdr_data(buff.begin(), buff.end());
        std::string_view extra;
        auto pos = hdr_data.find("\r\n\r\n");
        if (pos == hdr_data.npos)
            return {std::string_view()};

        extra = hdr_data.substr(pos + 4);
        hdr_data = hdr_data.substr(0, pos + 2);

        bool upgrade = false;
        bool conn_upgrade = false;
        std::string_view sock_key;
        bool version = false;
        auto fline = split_at(hdr_data, "\r\n");

        if (_mode == PeerOpMode::await_response)
        {
            auto proto = HeaderKey(split_at(fline, " "));
            auto code = split_at(fline, " ");

            if (code != "101" || proto != "HTTP/1.1")
                return std::nullopt;

            while (!hdr_data.empty())
            {
                auto val = split_at(hdr_data, "\r\n");
                HeaderKey key = trim(split_at(val, ":"));
                val = trim(val);

                if (key == "Connection")
                {
                    if (HeaderKey(val) != "Upgrade")
                        return std::nullopt;
                    conn_upgrade = true;
                }
                else if (key == "Upgrade")
                {
                    if (HeaderKey(val) != "websocket")
                        return std::nullopt;
                    upgrade = true;
                }
                else if (key == "Sec-WebSocket-Accept")
                {
                    sock_key = val;
                }
            }

            if (!upgrade || !conn_upgrade || sock_key != _ws_accept)
                return std::nullopt;
            _ws_accept.clear();
        }
        else
        {
            auto method = HeaderKey(split_at(fline, " "));
            auto path = split_at(fline, " ");

            if (method != "GET")
                return std::nullopt;

            while (!hdr_data.empty())
            {
                auto val = split_at(hdr_data, "\r\n");
                HeaderKey key = trim(split_at(val, ":"));
                val = trim(val);

                if (key == "Connection")
                {
                    if (HeaderKey(val) != "Upgrade")
                        return std::nullopt;
                    conn_upgrade = true;
                }
                else if (key == "Upgrade")
                {
                    if (HeaderKey(val) != "websocket")
                        return std::nullopt;
                    upgrade = true;
                }
                else if (key == "Sec-WebSocket-Key")
                {
                    sock_key = val;
                }
                else if (key == "Sec-WebSocket-Version")
                {
                    auto ver = std::strtoul(val.data(), nullptr, 10);
                    if (ver < 13)
                        return std::nullopt;
                    version = true;
                }
            }
            if (!upgrade || !conn_upgrade || sock_key.empty() || !version || path != _owner._config.endpoint_path)
            {
                try
                {
                    if (_owner._config.document_root)
                    {
                        handle_http_request(data, [&](std::string_view txt)
                                            {
                        if (!send_buffer(txt)) throw false; }, *_owner._config.document_root);
                    }
                }
                catch (...)
                {
                }

                return std::nullopt;
            }

            std::ostringstream resp;
            resp << "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Server: zerobus/1.0\r\n"
                    "Sec-WebSocket-Accept: "
                 << static_cast<std::string_view>(
                        ws::calculate_ws_accept(sock_key))
                 << "\r\n"
                    "\r\n";

            std::lock_guard _(_send_mx);
            if (!send_buffer(resp.view()))
            {
                return std::nullopt;
            }
        }
        _mode = PeerOpMode::message;
        _ws_parser.reset();
        this->send_reset();
        return extra;
    }

    std::pair<int, int> WsBridge::Peer::get_epoll_info() const
    {
        return {_sock.get(),
                EPOLLONESHOT | (_mode == PeerOpMode::connecting ? EPOLLOUT : EPOLLIN)};
    }

    void WsBridge::create_peer(int socket)
    {
        auto peer = std::make_unique<Peer>(*this, socket);
        auto [s, e] = peer->get_epoll_info();
        std::lock_guard _(_mx);
        auto handle = _handles.emplace(std::move(peer));
        _epoll.add(s, e, handle);
    }

    WsBridge::WsBridge(Bus bus, WsBridgeConfig config)
        : _bus(std::move(bus)), _config(std::move(config))
    {
        _epoll.add(_wakeup.get_fd(), EPOLLIN, 0);
    }

    WsBridge::~WsBridge()
    {
        _pool.request_stop();
        _wakeup.set();
        _pool.stop_threads();
    }

    void WsBridge::ensure_threads_running()
    {
        auto r = _config.threads - _pool.count();
        if (r)
            _pool.add_threads(r, [this](std::stop_token stp)
                              { worker(stp); });
    }

    WsBridge::Handle WsBridge::bind(const std::string &address_port)
    {
        int sock = create_listening_socket(address_port);
        auto server = std::make_unique<Server>(*this, sock);
        auto [s, e] = server->get_epoll_info();
        std::lock_guard _(_mx);
        auto handl = _handles.emplace(std::move(server));
        _epoll.add(s, e, handl);
        ensure_threads_running();
        return handl;
    }

    WsBridge::Handle WsBridge::connect(const std::string &address_port)
    {
        PPeer peer = std::make_unique<Peer>(*this, address_port);
        auto [s, e] = peer->get_epoll_info();
        std::lock_guard _(_mx);
        auto handl = _handles.emplace(std::move(peer));
        _epoll.add(s, e, handl);
        ensure_threads_running();
        return handl;
    }

    std::pair<int, int> WsBridge::Server::get_epoll_info() const
    {
        return {_sock.get(), EPOLLIN | EPOLLONESHOT};
    }

    void WsBridge::worker(std::stop_token stp)
    {
        while (!stp.stop_requested())
        {
            auto wt = _epoll.wait();
            if (!wt || wt->ident == 0)
                continue;
            PHandleData hdata;
            {
                std::unique_lock lk(_mx);
                auto iter = _handles.find(wt->ident);
                if (iter == _handles.end())
                    continue;
                hdata = iter->_value;
            }

            std::visit([&](auto ctx)
                       {
            auto s1 = ctx->get_epoll_info().first;
            if (ctx->on_epoll_event(wt->events)) {
                auto [s2,e] = ctx->get_epoll_info();
                if (!stp.stop_requested()) {
                    if (s1 == s2) {
                        _epoll.mod(s1, e, wt->ident);
                    } else {
                        _epoll.add(s2, e, wt->ident);
                    }
                }
            } else {
                std::unique_lock lk(_mx);
                _epoll.del(s1);
                _handles.erase(wt->ident);
            } }, hdata);
        }
    }

    void WsBridge::close(Handle h)
    {
        std::lock_guard _(_mx);
        auto iter = _handles.find(h);
        if (iter == _handles.end())
            return;
        std::visit([&](auto ptr)
                   {
        auto [s,e] = ptr->get_epoll_info();
        _epoll.del(s); }, iter->_value);
        _handles.erase(h);
    }

    std::unique_ptr<AbstractTransport> WsBridge::create_transport(Peer *peer,
                                                                  BinaryTransport<OutputTypeProxy<Peer *>> *&parser)
    {

        auto trn = std::make_unique<BinaryTransport<OutputTypeProxy<Peer *>>>(
            OutputTypeProxy<Peer *>(peer));

        parser = trn.get();

        if (_config.filter)
        {
            return _config.filter(std::move(trn));
        }
        else
        {
            return trn;
        }
    }

}
