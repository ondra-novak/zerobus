#include "ws_bridge_server.hpp"
#include "../../binary_transport.hpp"
#include <string>
#include <stdexcept>
#include <system_error>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
namespace zerobus {


int create_listening_socket(const std::string& address_port) {
    std::string host;
    std::string port;

    size_t colon_pos = address_port.find(':');
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

WsBridgeServer::WsBridgeServer(Bus bus,
        std::string address,
        WsBridgeConfig config)
        :_bus(std::move(bus))
        ,_mode(config.mode)
        ,_housekeeping_sec(config.housekeeping_sec)
{
    _listen_socket.reset(create_listening_socket(address));
    _listen_socket_handle = _peers.emplace(nullptr);

}

WsBridgeServer::~WsBridgeServer() {
}


void WsBridgeServer::do_housekeeping() {

    std::vector<std::unique_ptr<PeerContext> > _out_ctx;
    std::lock_guard _(_mx);
    auto now = std::chrono::system_clock::now();
    auto dur = std::chrono::seconds(_housekeeping_sec);
    for (auto iter = _peers.begin(); iter != _peers.end();) {
        if (iter->_value->get_last_activity()+dur < now) {
            _out_ctx.push_back(std::move(iter->_value));
            iter = _peers.erase(iter);
        } else {
            ++iter;
        }
    }
}


std::unique_ptr<AbstractTransport> WsBridgeServer::create_binary_transport(
        PeerContext *out,
        BinaryTransport<OutputTypeProxy<PeerContext *> >  *& in) {

    auto ptr = std::make_unique<BinaryTransport<OutputTypeProxy<PeerContext *> > >(
            OutputTypeProxy<PeerContext *>{out});
    in = ptr.get();
    return ptr;
}



WsBridgeServer::PeerContext::PeerContext(WsBridgeServer &owner, Socket socket)
    :_owner(owner)
    ,_socket(std::move(socket))
    ,_ws_parser(_in_buffer)
    ,_last_activity(std::chrono::system_clock::now())
{
    _br.emplace(_owner._bus, create_binary_transport(this, _parser), _owner._mode);
}

constexpr std::size_t ws_frame_reserve = 16;

char* WsBridgeServer::PeerContext::output_start(std::size_t sz) {
    std::unique_lock lk(_wmx);
    _tmp_buff_size = _out_buffer.size();
    _out_buffer.resize(_tmp_buff_size+sz+ws_frame_reserve);
    lk.release();
    return  _out_buffer.data()+_tmp_buff_size+ws_frame_reserve;
}

void WsBridgeServer::PeerContext::send_buffer_to_socket() {
    if (_socket) {
        std::string_view s(_out_buffer.data(), _out_buffer.size());
        while (!s.empty()) {
            int r = ::send(_socket.get(), s.data(), s.size(), 0);
            if (r <= 0) {
                int err = errno;
                if (err == EINTR) continue;
                _socket = {};
                return;
            }
            s = s.substr(r);
        }
        _out_buffer.clear();
        _last_activity = std::chrono::system_clock::now();
    }
}

void WsBridgeServer::PeerContext::output_commit(std::size_t sz) {
    std::unique_lock lk(_wmx, std::adopt_lock);
    char *b = _out_buffer.data()+_tmp_buff_size;
    ws::build({std::string_view(b+ws_frame_reserve, sz), ws::Type::binary},
            [&](char c){*b++ = c;});
    _out_buffer.resize(b - _out_buffer.data());
    send_buffer_to_socket();
}

void WsBridgeServer::PeerContext::send(const ws::Message &msg) {
    ws::build(msg, [&](char c){_out_buffer.push_back(c);});
    send_buffer_to_socket();
}

void WsBridgeServer::PeerContext::update_socket(Socket socket,
        std::string_view initial_data) {

    _socket = std::move(socket);
    _ws_parser.reset();
    _in_buffer.clear();
    if (!initial_data.empty()) {
        on_ws_data(initial_data);
    }

}

bool WsBridgeServer::PeerContext::on_ws_data(std::string_view data) {
    bool ok = _ws_parser.push_data(data);
    while (ok) {
        auto msg = _ws_parser.get_message();
        switch(msg.type) {
            case ws::Type::binary:
                _parser->parse(msg.payload);
                break;
            case ws::Type::connClose:
                send(ws::Message{"", ws::Type::connClose, ws::Base::closeNormal});
                _socket = {};
                return true;
            case ws::Type::ping:
                send(ws::Message{msg.payload, ws::Type::pong});
                break;
            default:
                break;
        }
        ok = _ws_parser.reset_parse_next();
    }
    return true;
}

bool WsBridgeServer::PeerContext::on_incoming_data(Handle h) {

    char buff[1500];
    int r = ::recv(_socket.get(), buff, 1500, MSG_DONTWAIT);
    if (r < 0) {
        int err = errno;
        if (err == EWOULDBLOCK) {
            _owner.listen_on_socket(_socket.get(), h);
            return true;
        }
        _socket = {}; //error, close socket
        return true;
    } else if (r == 0) {
        _socket = {}; //error, close socket
        return true;
    } else {
        if (_awaiting_header) {
            _in_buffer.insert(_in_buffer.end(), buff, buff+r);
            if (! parse_header()) return false;
        } else {
            if (!on_ws_data(std::string_view(buff,r))) return false;
        }
        _owner.listen_on_socket(_socket.get(), h);
    }
    return true;
}

bool WsBridgeServer::PeerContext::parse_header() {
    std::string_view hdr_data (_in_buffer.data(), _in_buffer.size());
    std::string_view extra;
    auto pos = hdr_data.find("\r\n\r\n");
    if (pos == hdr_data.npos) return true;

    extra = hdr_data.substr(pos);
    hdr_data = hdr_data.substr(0, pos-2);

    auto fline = split_at(hdr_data,"\r\n");
    auto method = split_at(fline, " ");
    auto ident = split_at(fline, " ");

    bool upgrade = false;
    bool conn_upgrade = false;
    std::string_view sock_key;
    bool version = false;

    if (HeaderKey(method) != "GET") return false;

    while (!hdr_data.empty()) {
        auto val = split_at(fline, "\r\n");
        HeaderKey key = trim(split_at(val, ":"));
        val = trim(val);

        if (key == "Connection") {
            if (HeaderKey(val) != "Upgrade") return false;
            conn_upgrade = true;
        }
        else if (key == "Upgrade") {
            if (HeaderKey(val) != "websocket") return false;
            upgrade = true;
        }
        else if (key == "Sec-WebSocket-Key") {
            sock_key = val;
        }
        else if (key == "Sec-WebSocket-Version") {
            auto  ver = std::strtoul(key.data(), nullptr, 10);
            if (ver < 13) return false;
        }
    }
    if (!upgrade || !conn_upgrade || sock_key.empty() || !version) return false;

    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: "
            << static_cast<std::string_view>(
                    ws::calculate_ws_accept(sock_key)) << "\r\n";
    _out_buffer.clear();
    auto resps = resp.view();
    _out_buffer.insert(_out_buffer.end(), resps.begin(), resps.end());
    send_buffer_to_socket();
    _in_buffer.clear();
    _ws_parser.reset();
    if (_socket) {
        _awaiting_header = false;
        if (_owner.connect_identity(_socket, ident, extra)) {
            return false;
        }
        if (!extra.empty()) return on_ws_data(extra);
        return true;
    }
    return false;
}


}

