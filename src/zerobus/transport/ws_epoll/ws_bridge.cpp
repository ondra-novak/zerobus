#include "ws_bridge.hpp"
#include "../../utils/stack_alloc.hpp"
#include "../utils/http_utils.hpp"
#include "../../utils/random_channel_gen.hpp"
#include "webserver.hpp"
#include "epoll_dispatcher.hpp"

#ifdef WITH_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <netdb.h>
#include <string>
#include <format>

template class EpollDispatcher<void>;
template PEpollDispatcher<void> create_multithreaded(unsigned int threads);
template PEpollDispatcher<void> create_singlethreaded();

namespace zerobus {

void WsBridge::Shared::SSL_CTX_Deleter::operator ()([[maybe_unused]] SSL_CTX *  _) {
#ifdef WITH_TLS
    SSL_CTX_free(_);
#endif
}
void WsBridge::Peer::SSL_Deleter::operator()([[maybe_unused]] SSL *_){
#ifdef WITH_TLS
    SSL_free(_);
#endif
}
#ifdef WITH_TLS

void keylog_callback(const SSL *, const char *line) {
    static FILE* keylog = nullptr;
    static std::once_flag init_flag;
    std::call_once(init_flag, [] {
        const char* path = std::getenv("SSLKEYLOGFILE");
        if (path) {
            keylog = fopen(path, "a");
        }
    });

    if (keylog) {
        fprintf(keylog, "%s\n", line);
        fflush(keylog);
    }
}


SSLError::SSLError(const std::string& msg)
    : std::runtime_error(msg + ": " + getOpenSSLErrors()) {}
std::string SSLError::getOpenSSLErrors() {
    std::string errors;
    unsigned long errCode = 0;
    while ((errCode = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(errCode, buf, sizeof(buf));
        if (!errors.empty()) errors += "\n";
        errors += buf;
    }
    return errors.empty() ? "No OpenSSL error" : errors;
}

#endif



int create_listening_socket(const std::string &address_port) {
    std::string host;
    std::string port;

    size_t colon_pos = address_port.rfind(':');
    if (colon_pos == std::string::npos
            || colon_pos == address_port.length() - 1) {
        throw std::invalid_argument(
                "Invalid address format. Expected format: [host]:port or :port");
    }

    host = address_port.substr(0, colon_pos);
    port = address_port.substr(colon_pos + 1);

    addrinfo hints { };
    hints.ai_family = AF_UNSPEC;     // IPv4 nebo IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // Pro listen socket

    addrinfo *result;
    int ret = getaddrinfo(host.empty() ? nullptr : host.c_str(), port.c_str(),
            &hints, &result);
    if (ret != 0) {
        throw std::system_error(0, std::generic_category(),
                std::string("getaddrinfo: ") + gai_strerror(ret));
    }

    int sockfd = -1;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family,
                rp->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK,
                rp->ai_protocol);
        if (sockfd == -1)
            continue;

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            if (listen(sockfd, SOMAXCONN) == 0) {
                freeaddrinfo(result);
                return sockfd;
            }
        }

        // jinak zavřít a zkusit další
        ::close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);
    throw std::system_error(errno, std::generic_category(),
            "Failed to bind and listen on any address");
}

int connectToAddress(const std::string &addressPort) {
    std::string host;
    std::string port;

    // Rozdělení na adresu a port
    auto colonPos = addressPort.rfind(':');
    if (colonPos == std::string::npos) {
        throw std::invalid_argument(
                "Invalid address format. Expected format: host:port");
    }
    host = addressPort.substr(0, colonPos);
    port = addressPort.substr(colonPos + 1);

    struct addrinfo hints { };
    struct addrinfo *resptr = nullptr;
    ;
    std::unique_ptr<struct addrinfo, decltype([](struct addrinfo *x) {
                freeaddrinfo(x);})> result;
    int sockfd = -1;

    hints.ai_family = AF_UNSPEC;     // IPv4 nebo IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int s = getaddrinfo(host.c_str(), port.c_str(), &hints, &resptr);
    result.reset(resptr);

    if (s != 0) {
        throw std::system_error(errno, std::generic_category(),
                "getaddrinfo failed: " + std::string(gai_strerror(s)));
    }

    int e = 1;
    for (struct addrinfo *rp = result.get(); rp != nullptr; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family,
                rp->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK,
                rp->ai_protocol);
        if (sockfd == -1)
            continue;

        int r = connect(sockfd, rp->ai_addr, rp->ai_addrlen);
        if (r != -1)
            return sockfd;
        e = errno;
        if (e == EINPROGRESS)
            return sockfd;

        ::close(sockfd);
        sockfd = -1;
    }

    throw std::system_error(e, std::generic_category(), "Connect failed");
}

int create_reconnect_timer() {
    int fd = timerfd_create(CLOCK_MONOTONIC, O_CLOEXEC);
    struct itimerspec ts = { };
    ts.it_value = { 5, 0 };
    timerfd_settime(fd, 0, &ts, NULL);
    return fd;
}

void WsBridge::SocketDeleter::operator()(int int1) {
    ::close(int1);
}

int WsBridge::Server::on_epoll_event(int) {
    int newsock = accept4(_sock.get(), nullptr, nullptr,
    SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (newsock != -1) {
        _owner.create_peer(newsock);
    }
    return 1;
}

WsBridge::Server::Server(WsBridge &owner, int socket) :
        _owner(owner), _sock(socket, { }) {
}

WsBridge::Peer::Peer(WsBridge &owner) :
        Bridge(owner._bus, owner.create_transport(this, _bridge_parser)), _shared(
                owner._shared), _ws_parser(_input_buffer, false) {
}

WsBridge::Peer::Peer(WsBridge &owner, int socket) :
        Peer(owner) {
    _sock.reset(socket);
    _mode = PeerOpMode::await_request;

    if (_shared->_ssl_server_ctx) {
#ifdef WITH_TLS
        _ssl_sock.reset(SSL_new(_shared->_ssl_server_ctx.get()));
        SSL_set_fd(_ssl_sock.get(), _sock.get());
        SSL_set_accept_state(_ssl_sock.get());
        _need_handshake = true;
#endif
    }
}
WsBridge::Peer::Peer(WsBridge &owner, std::string address) :
        Peer(owner) {

    _mode = PeerOpMode::connecting;
    connect(address);
}

char* WsBridge::Peer::output_start(std::size_t sz, MsgFlags imp) {
    std::unique_lock lk(_send_mx);
    if (_mode != PeerOpMode::message)
        return nullptr;
    if (contains<MsgFlags::priorityLow>(imp) && !_output_buffer.empty())
        return nullptr;
    if (!contains<MsgFlags::priorityHigh>(imp)
            && _output_buffer.size() >= _shared->_config.hwm_bytes)
        return nullptr;
    _build_buffer.clear();
    _build_buffer.resize(sz);
    lk.release();
    return _build_buffer.data();
}

DeliveryError WsBridge::Peer::output_commit(std::size_t sz, MsgFlags imp) {
    std::unique_lock lk(_send_mx, std::adopt_lock);
    _build_buffer.resize(sz);
    std::string_view data(_build_buffer.data(), _build_buffer.size());
    bool res = send_message(lk, ws::Message(data, ws::Type::binary), imp);
    if (!res)
        shutdown(_sock.get(), SHUT_RD); //simulate closed connection
    return res ? DeliveryError::send_timeout : DeliveryError::not_used;
}

bool WsBridge::Peer::flush_buffer() {
    int r;
#ifdef WITH_TLS
    if (_ssl_sock) {
        //no need lock, send is always locked
        r = SSL_write(_ssl_sock.get(),_output_buffer.data(), _output_buffer.size());
        int s = process_ssl_error(r);
        if (s <= 0) return s == 0;
    } else
#endif
    {
        r = ::send(_sock.get(), _output_buffer.data(), _output_buffer.size(),
           MSG_DONTWAIT|MSG_NOSIGNAL);
    }
    if (r == 0)
        return false;
    if (r < 0) {
        int e = errno;
        if (e != EWOULDBLOCK)
            return false;
    } else {
        _output_buffer.erase(_output_buffer.begin(),
                _output_buffer.begin() + r);
    }
    return true;
}

bool WsBridge::Peer::send_message(std::unique_lock<std::mutex> &lk,
        const ws::Message &msg, MsgFlags imp) {

    uint8_t masking[4];
    uint8_t *masking_ptr = { };
    if (_mask_rnd) {
        masking_ptr = masking;
        std::uniform_int_distribution<int> dist(0x00, 0xFF);
        for (int i = 0; i < 4; ++i) {
            masking_ptr[i] = static_cast<std::uint8_t>(dist(*_mask_rnd));
        }
    }

    ws::build(msg, [&](char c) {_output_buffer.push_back(c);}, masking_ptr);

    return finish_send(lk, imp);
}

bool WsBridge::Peer::conn_error() {
    std::unique_lock<std::mutex> lk(_send_mx);
    return conn_error(lk);
}
bool WsBridge::Peer::conn_error(std::unique_lock<std::mutex> &lk) {
    if (_reconnect_addr.empty())
        return false;
    {
        if (!lk.owns_lock()) lk.lock();
        _mode = PeerOpMode::reconnect;
        _sock.reset(create_reconnect_timer());
        lk.unlock();
    }
    this->disconnect();
    return true;
}

bool WsBridge::Peer::on_epoll_in() noexcept {
    if (_mode == PeerOpMode::reconnect)[[unlikely]] {
        try {
            connect(_reconnect_addr);
            return true;
        } catch (...) {
            return conn_error();
        }
    }
    while (true) {
        char buff[4096];
        int r;
    #ifdef WITH_TLS
        if (_ssl_sock) {
            int s;
            {
                std::lock_guard _(_send_mx);
                r = SSL_read(_ssl_sock.get(), buff, sizeof(buff));
                s = process_ssl_error(r);
            }
            if (s < 0) return conn_error();
            if (s == 0) return true;
        } else
    #endif
        {
            r = ::recv(_sock.get(), buff, sizeof(buff), MSG_DONTWAIT|MSG_NOSIGNAL);
        }
        if (r < 0) {
            int e = errno;
            if (e == EWOULDBLOCK)
                return true;
            return conn_error();
        } else if (r == 0) {
            return conn_error();
        } else {
            bool ok = false;
            auto data = std::string_view(buff, r);
            if (_mode != PeerOpMode::message) {
                auto extra = read_http_header(data);
                if (!extra) {
                    return conn_error();
                }
                if (!extra->empty()) {
                    ok = _ws_parser.push_data(*extra);
                }
            } else {
                ok = _ws_parser.push_data(data);
            }
            _kl = 0;
            while (ok) {
                auto msg = _ws_parser.get_message();
                switch (msg.type) {
                    case ws::Type::binary:
                        _bridge_parser->parse(msg.payload);
                        break;
                    case ws::Type::connClose: {
                        std::unique_lock lk(_send_mx);
                        send_message(lk, { "", ws::Type::connClose,
                                ws::Base::closeNormal }, MsgFlags::priorityNormal);
                        return conn_error();
                    }
                    case ws::Type::ping: {
                        std::unique_lock lk(_send_mx);
                        send_message(lk, { msg.payload, ws::Type::pong },
                                MsgFlags::priorityHigh);
                        break;
                    }
                    default:
                        break;
                }
                ok = _ws_parser.reset_parse_next();
            }
        }
    }

}

bool WsBridge::Peer::on_epoll_out() noexcept {

    std::unique_lock lk(_send_mx);
    if (_mode == PeerOpMode::message) {
        flush_buffer();
    } else {
        int error = 1;
        socklen_t len = sizeof(error);
        getsockopt(_sock.get(), SOL_SOCKET, SO_ERROR, &error, &len);
        if (error != 0 || !send_ws_request()) {
            _mode = PeerOpMode::reconnect;
            _sock.reset(create_reconnect_timer());
        } else {
            _mode = PeerOpMode::await_response;
        }
    }
    return 1;
}

#ifdef WITH_TLS
int WsBridge::Peer::process_ssl_error(int ret_code) noexcept {
    _ssl_want_mode = 0;
    int err = SSL_get_error(_ssl_sock.get(), ret_code);
    switch (err) {
        case SSL_ERROR_NONE: return 1;
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ: _ssl_want_mode = err; return 0;
        default:return -1;
    }
}
#endif

int WsBridge::Peer::on_epoll_event(int event) noexcept {
    if (_in_handler.test_and_set())
        return 0;
    int r = 1;
#ifdef WITH_TLS
    if (_need_handshake) {
        std::unique_lock lk(_send_mx);
        r = process_ssl_error(SSL_do_handshake(_ssl_sock.get()));
        if (r > 0) {
            _need_handshake = false;
            if (_mode == PeerOpMode::connecting) {
                long verify_result = SSL_get_verify_result(_ssl_sock.get());
                if (verify_result != X509_V_OK) {
                    r = conn_error(lk)?1:-1;
                }
            }
        } else if (r < 0) {
            r =  conn_error(lk)?1:-1;
        } else {
            r = 1;
        }
    } else
#endif
    {

        if (event & EPOLLIN) {
            r = on_epoll_in() ? 1 : -1;
        }
        if (event & EPOLLOUT) {
            r = on_epoll_out() ? 1 : -1;
        }
    }
    _in_handler.clear();
    return r;
}

void WsBridge::Peer::connect(std::string address) {
    std::lock_guard _(_send_mx);
    _mask_rnd.emplace(std::random_device()());
    _output_buffer.clear();
    _input_buffer.clear();
    _ws_parser.reset();
    _mode = PeerOpMode::connecting;
    _reconnect_addr = std::move(address);
    _sock.reset(connectToAddress(_reconnect_addr));
    if (_shared->_ssl_client_ctx) {
#ifdef WITH_TLS
        _ssl_sock.reset(SSL_new(_shared->_ssl_client_ctx.get()));
        SSL_set_fd(_ssl_sock.get(), _sock.get());
        SSL_set_connect_state(_ssl_sock.get());
        _need_handshake = true;
#endif
    }

}


bool WsBridge::Peer::send_ws_request() {
    auto key = ws::generate_ws_key();
    _ws_accept = ws::calculate_ws_accept(key);
    _output_buffer.clear();
    std::format_to(std::back_inserter(_output_buffer),
            "GET / HTTP/1.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Host: {} \r\n"
            "User-Agent: zerobus/1.0\r\n"
            "Sec-WebSocket-Key: {}" "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
             "\r\n", _reconnect_addr, static_cast<std::string_view>(key));
    return flush_buffer();
}

bool WsBridge::Peer::direct_send(std::string_view data) {
    std::unique_lock<std::mutex> lk(_send_mx);
    _output_buffer.insert(_output_buffer.end(), data.begin(), data.end());
    return finish_send(lk, MsgFlags::priorityHigh);
}
bool WsBridge::Peer::finish_send(std::unique_lock<std::mutex> &lk, MsgFlags imp) {
    auto tm = std::chrono::system_clock::now()
            + std::chrono::milliseconds(_shared->_config.send_timeout_ms);

    if (!flush_buffer()) return false;

    if (!_in_handler.test_and_set()) {
        auto [s, e] = get_epoll_info();
        _shared->_epoll.mod(s, e, _h);
       _in_handler.clear();
    }
    if (!contains<MsgFlags::priorityHigh>(imp)) return true;
    bool r = _cv.wait_until(lk, tm, [&] {
        return _output_buffer.size() < _shared->_config.hwm_bytes;
    });
    return r;

}


std::optional<std::string_view> WsBridge::Peer::read_http_header(
        std::string_view data) {

    auto &buff = _input_buffer;
    buff.insert(buff.end(), data.begin(), data.end());
    std::string_view hdr_data(buff.begin(), buff.end());
    std::string_view extra;
    auto pos = hdr_data.find("\r\n\r\n");
    if (pos == hdr_data.npos)
        return {std::string_view()};

    extra = hdr_data.substr(pos + 4);
    hdr_data = hdr_data.substr(0, pos + 2);
    auto hdr_data_saved = hdr_data;

    bool upgrade = false;
    bool conn_upgrade = false;
    std::string_view sock_key;
    bool version = false;
    auto fline = split_at(hdr_data, "\r\n");

    if (_mode == PeerOpMode::await_response) {
        auto proto = HeaderKey(split_at(fline, " "));
        auto code = split_at(fline, " ");

        if (code != "101" || proto != "HTTP/1.1")
            return std::nullopt;

        while (!hdr_data.empty()) {
            auto val = split_at(hdr_data, "\r\n");
            HeaderKey key = trim(split_at(val, ":"));
            val = trim(val);

            if (key == "Connection") {
                if (HeaderKey(val) != "Upgrade")
                    return std::nullopt;
                conn_upgrade = true;
            } else if (key == "Upgrade") {
                if (HeaderKey(val) != "websocket")
                    return std::nullopt;
                upgrade = true;
            } else if (key == "Sec-WebSocket-Accept") {
                sock_key = val;
            }
        }

        if (!upgrade || !conn_upgrade || sock_key != _ws_accept)
            return std::nullopt;
        _ws_accept.clear();
    } else {
        auto method = HeaderKey(split_at(fline, " "));
        auto path = split_at(fline, " ");
        bool err = false;

        if (method != "GET")
            return std::nullopt;

        while (!hdr_data.empty()) {
            auto val = split_at(hdr_data, "\r\n");
            HeaderKey key = trim(split_at(val, ":"));
            val = trim(val);

            if (key == "Connection") {
                if (HeaderKey(val) != "Upgrade")
                    err = true;
                conn_upgrade = true;
            } else if (key == "Upgrade") {
                if (HeaderKey(val) != "websocket")
                    err = true;
                upgrade = true;
            } else if (key == "Sec-WebSocket-Key") {
                sock_key = val;
            } else if (key == "Sec-WebSocket-Version") {
                auto ver = std::strtoul(val.data(), nullptr, 10);
                if (ver < 13)
                    err = true;
                version = true;
            }
        }
        if (!upgrade || !conn_upgrade || sock_key.empty() || !version || err
                || path != _shared->_config.endpoint_path) {
            try {
                if (_shared->_config.document_root) {
                    if (handle_http_request(hdr_data_saved, [&](std::string_view txt) {
                        return direct_send(txt);
                    }, *_shared->_config.document_root)) {
                        _input_buffer.erase(_input_buffer.begin(), _input_buffer.begin()+hdr_data_saved.size()+2);
                        return std::string_view();
                    }
                }
            } catch (...) {
            }

            return std::nullopt;
        }

        std::lock_guard _(_send_mx);
        _output_buffer.clear();
        std::format_to(std::back_inserter(_output_buffer),
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Server: zerobus/1.0\r\n"
                "Sec-WebSocket-Accept: {} \r\n"
                "\r\n",static_cast<std::string_view>(ws::calculate_ws_accept(sock_key)));
        if (!flush_buffer()) return std::nullopt;
    }
    _mode = PeerOpMode::message;
    _ws_parser.reset();
    this->send_reset();
    return extra;
}

std::pair<int, int> WsBridge::Peer::get_epoll_info() const {
#ifdef WITH_TLS
    if (_ssl_sock) {
        std::lock_guard _(_send_mx);
        switch (_ssl_want_mode){
            case SSL_ERROR_WANT_READ: return {_sock.get(), EPOLLONESHOT|EPOLLIN};
            case SSL_ERROR_WANT_WRITE: return {_sock.get(), EPOLLONESHOT|EPOLLOUT};
            default:break;
        }
    }
#endif
    return {_sock.get(),
        EPOLLONESHOT |
        (_mode == PeerOpMode::connecting ? EPOLLOUT : EPOLLIN)
        |(_output_buffer.empty() ? static_cast<EPOLL_EVENTS>(0) : EPOLLOUT)
    };
}

void WsBridge::create_peer(int socket) {
    auto peer = std::make_unique<Peer>(*this, socket);
    auto [s, e] = peer->get_epoll_info();
    std::lock_guard _(_mx);
    auto p = peer.get();
    auto handle = _handles.emplace(std::move(peer));
    _shared->_epoll.add(s, e, handle);
    p->set_handle(handle);

}

WsBridge::WsBridge(Bus bus, WsBridgeConfig config)
        :_bus(std::move(bus))
        ,_shared(std::make_shared<Shared>(std::move(config))) {
    _shared->_epoll.add(_shared->_wakeup.get_fd(), EPOLLIN, 0);

    if (_shared->_config.use_tls) {
#ifdef WITH_TLS
        static std::once_flag ssl_init;
        std::call_once(ssl_init, []{
                SSL_library_init();
                SSL_load_error_strings();
                OpenSSL_add_all_algorithms();
        });


        _shared->_ssl_client_ctx.reset(SSL_CTX_new(TLS_client_method()));
        if (_shared->_config.certificate_pem) {
            if(!SSL_CTX_load_verify_locations(_shared->_ssl_client_ctx.get(),
                    _shared->_config.certificate_pem->c_str(),NULL))
                    throw SSLError("SSL_CTX_load_verify_locations");
        } else {
            if (!SSL_CTX_set_default_verify_paths(_shared->_ssl_client_ctx.get()))
                throw SSLError("SSL_CTX_set_default_verify_paths");
        }
        SSL_CTX_set_verify(_shared->_ssl_client_ctx.get(), SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_keylog_callback(_shared->_ssl_client_ctx.get(), keylog_callback);

        if (_shared->_config.private_key_pem) {
            if (!_shared->_config.certificate_pem) throw SSLError("Missing certificate (in config)");
            _shared->_ssl_server_ctx.reset(SSL_CTX_new(TLS_server_method()));

            if (!SSL_CTX_use_certificate_file(_shared->_ssl_server_ctx.get(),
                    _shared->_config.certificate_pem->c_str(), SSL_FILETYPE_PEM))
                throw SSLError("Failed to load certificate");

            if (!SSL_CTX_use_PrivateKey_file(_shared->_ssl_server_ctx.get(),
                     _shared->_config.private_key_pem->c_str(), SSL_FILETYPE_PEM))
                throw SSLError("Failed to load private key");

            if (!SSL_CTX_check_private_key(_shared->_ssl_server_ctx.get()))
                throw SSLError("Private key does not match the certificate");
            SSL_CTX_set_keylog_callback(_shared->_ssl_server_ctx.get(), keylog_callback);
        }

#else
        throw std::invalid_argument("Cannot use TLS: SSL support not compiled in.");
#endif
    }
}

WsBridge::~WsBridge() {
    _pool.request_stop();
    _shared->_wakeup.set();
    _pool.stop_threads();
}

void WsBridge::ensure_threads_running() {
    auto r = _shared->_config.threads - _pool.count();
    if (r)
        _pool.add_threads(r, [this](std::stop_token stp) {
            worker(stp);
        });
}

WsBridge::Handle WsBridge::bind(const std::string &address_port) {
    int sock = create_listening_socket(address_port);
    auto server = std::make_unique<Server>(*this, sock);
    auto [s, e] = server->get_epoll_info();
    std::lock_guard _(_mx);
    auto handl = _handles.emplace(std::move(server));
    _shared->_epoll.add(s, e, handl);
    ensure_threads_running();
    return handl;
}

WsBridge::Handle WsBridge::connect(const std::string &address_port) {
    PPeer peer = std::make_unique<Peer>(*this, address_port);
    auto [s, e] = peer->get_epoll_info();
    std::lock_guard _(_mx);
    auto p = peer;
    auto handl = _handles.emplace(std::move(peer));
    _shared->_epoll.add(s, e, handl);
    p->set_handle(handl);
    ensure_threads_running();
    return handl;
}

std::pair<int, int> WsBridge::Server::get_epoll_info() const {
    return {_sock.get(), EPOLLIN | EPOLLONESHOT};
}

void WsBridge::worker(std::stop_token stp) {
    auto tm = _next_hk.exchange(std::chrono::system_clock::time_point::min());
    while (!stp.stop_requested()) {
        auto wt = _shared->_epoll.wait(tm);
        if (!wt) {
            if (_shared->_config.send_timeout_ms) {
                housekeeping();
                tm = std::chrono::system_clock::now()+std::chrono::seconds(_shared->_config.keep_alive_interval_sec);
            }
            continue;
        }
        if (wt->ident == 0)
            continue;
        PHandleData hdata;
        {
            std::unique_lock lk(_mx);
            auto iter = _handles.find(wt->ident);
            if (iter == _handles.end())
                continue;
            hdata = iter->_value;
        }

        std::visit([&](auto ctx) {
            auto s1 = ctx->get_epoll_info().first;
            int r = ctx->on_epoll_event(wt->events);
            if (r > 0) {
                auto [s2, e] = ctx->get_epoll_info();
                if (!stp.stop_requested()) {
                    if (s1 == s2) {
                        _shared->_epoll.mod(s1, e, wt->ident);
                    } else {
                        _shared->_epoll.add(s2, e, wt->ident);
                    }
                }
            } else if (r < 0) {
                std::unique_lock lk(_mx);
                _handles.erase(wt->ident);
            }
        }, hdata);
    }
}

void WsBridge::close(Handle h) {
    std::lock_guard _(_mx);
    auto iter = _handles.find(h);
    if (iter == _handles.end())
        return;
    _handles.erase(h);
}

std::unique_ptr<AbstractTransport> WsBridge::create_transport(Peer *peer,
        BinaryTransport<OutputTypeProxy<Peer*>> *&parser) {

    auto trn = std::make_unique < BinaryTransport<OutputTypeProxy<Peer*>>
            > (OutputTypeProxy<Peer*>(peer));

    parser = trn.get();

    if (_shared->_config.filter) {
        return _shared->_config.filter(std::move(trn));
    } else {
        return trn;
    }
}

void WsBridge::housekeeping() {
    std::vector<PHandleData> to_destroy ={};
    std::lock_guard _(_mx);
    for (auto iter = _handles.begin(); iter !=_handles.end();) {
        std::visit([&](auto &v){
            if (!v->keep_alive()) {
                to_destroy.push_back(std::move(iter->_value));
                iter = _handles.erase(iter);
            } else {
                ++iter;
            }
        },iter->_value);
    }
}

bool WsBridge::Peer::keep_alive() {
    std::unique_lock lk(_send_mx);
    if (_mode == PeerOpMode::reconnect) return true;
    if (_kl == 2) return false;
    if (++_kl == 2) send_message(lk, ws::Message("",ws::Type::ping), MsgFlags::priorityHigh);
    return true;
}

}

