#include "bridge_tcp_server.h"

#include "bridge.h"
#include "sha1.h"
#include "base64.h"
#include <charconv>

namespace zerobus {

BridgeTCPServer::BridgeTCPServer(Bus bus, std::shared_ptr<INetContext> ctx, std::string address_port, AuthConfig auth_cfg)
        :_bus(bus),_ctx(std::move(ctx)),_auth_cfg(std::move(auth_cfg)) {
        _aux = _ctx->create_server(address_port);
        auto br = IBridgeAPI::from_bus(_bus.get_handle());
        br->register_monitor(this);
        _next_ping = std::chrono::system_clock::now()+std::chrono::seconds(_ping_interval);
        _ctx->set_timeout(_aux, _next_ping, this);
        _ctx->accept(_aux, this);
    }

BridgeTCPServer::BridgeTCPServer(Bus bus, std::string address_port, AuthConfig auth_cfg)
    :BridgeTCPServer(std::move(bus), make_context(1), std::move(address_port), std::move(auth_cfg)) {
}

BridgeTCPServer::~BridgeTCPServer() {
    std::unique_lock lk(_mx);
    auto p = std::move(_peers);
    lk.unlock();
    p.clear();
    lk.lock();
    auto br = IBridgeAPI::from_bus(_bus.get_handle());
    br->unregister_monitor(this);
    _ctx->destroy(_aux);
}


void BridgeTCPServer::on_channels_update() noexcept {
    _ctx->set_timeout(_aux, std::chrono::system_clock::time_point::min(), this);
}

bool BridgeTCPServer::on_message_dropped(IListener *, const Message &) noexcept {return false;}

void BridgeTCPServer::on_accept(ConnHandle aux, std::string /*peer_addr*/) noexcept {
    //TODO report peer_addr
    std::lock_guard _(_mx);
    auto p = std::make_unique<Peer>(*this, aux, _id_cntr++);
    _peers.push_back(std::move(p));
    _ctx->accept(_aux, this);
}


void BridgeTCPServer::on_timeout() noexcept {
    auto now  = std::chrono::system_clock::now();
    std::vector<std::shared_ptr<Peer> > pcpy;
    {
        std::lock_guard _(_mx);
        if (now >= _next_ping) {
            _next_ping = now + std::chrono::seconds(_ping_interval);
            _peers.erase(std::remove_if(
                    _peers.begin(), _peers.end(), [&](const auto &p) {
                Peer &x = *p;
                return x.check_dead();
            }),_peers.end());
        }
        for (const auto &x: pcpy) {
            if (!x->disabled()) {
                x->send_mine_channels();
            }
        }
    }
    _ctx->set_timeout(_aux, _next_ping, this);
}

BridgeTCPServer::Peer::Peer(BridgeTCPServer &owner, ConnHandle aux, unsigned int id)
    :BridgeTCPCommon(owner._bus, owner._ctx, aux)
    ,_format(Format::unknown)
    ,_id(id)
    ,_owner(owner)
    ,_ws_parser(BridgeTCPCommon::_input_data)
    {
    init();
}

void BridgeTCPServer::Peer::initial_handshake() {
    Peer::send_welcome();
    Peer::send_mine_channels();
}

void BridgeTCPServer::Peer::on_auth_response(std::string_view ident, std::string_view proof, std::string_view salt) {
    _owner.on_auth_response(this, ident, proof, salt);
}

bool BridgeTCPServer::Peer::check_dead() {
    if (_activity_check) {
        if (_ping_sent) return true;
        send_ping();
        _ping_sent = true;
    } else {
        _ping_sent = false;
    }
    _activity_check = true;
    return false;
}

void BridgeTCPServer::Peer::lost_connection() {
    _owner.lost_connection(this);
}

template<typename Fn>
void BridgeTCPServer::call_with_peer(unsigned int id, Fn &&fn) {
    std::lock_guard _(_mx);
    auto iter = std::find_if(_peers.begin(), _peers.end(), [&](const auto &x) {
        return x->get_id() == id;
    });
    if (iter != _peers.end()) {
        fn(iter->get());
    }
}

void BridgeTCPServer::on_auth_response(Peer *p, std::string_view ident, std::string_view proof, std::string_view salt) {
    _auth_cfg.verify_fn(this, AuthInfo{p->get_id(),ident,proof,salt});
}

void BridgeTCPServer::accept_auth(unsigned int id) {
    call_with_peer(id, [&](Peer *p){
        p->initial_handshake();
    });
}

void BridgeTCPServer::accept_auth(unsigned int id, ChannelFilter flt) {
    call_with_peer(id, [&](Peer *p){
        p->set_filter(std::move(flt));
        p->initial_handshake();
    });
}

void BridgeTCPServer::reject_auth(unsigned int id) {
    call_with_peer(id, [&](Peer *p){
        p->send_auth_failed();
    });
}

void BridgeTCPServer::lost_connection(Peer *p) {
    std::lock_guard _(_mx);
    auto ne = std::remove_if(_peers.begin(), _peers.end(), [&](const auto &x){
        return x.get() == p;
    });
    _peers.erase(ne, _peers.end());
}

void BridgeTCPServer::Peer::receive_complete(std::string_view data) noexcept {
    _activity_check = false;
    switch (_format) {
        default:
        case Format::unknown:
            std::copy(data.begin(), data.end(), std::back_inserter(_input_data));
            if (_input_data.size() >= magic.size()) {
                std::size_t prev_size = _input_data.size() - data.size();
                std::string_view t(_input_data.data(), magic.size());
                if (t == magic) {
                    _format = Format::zbus;
                    _input_data.clear();
                    data = data.substr(magic.size()-prev_size);
                    if (!data.empty()) {
                        receive_complete(data);
                    } else {
                        read_from_connection();
                    }
                    start_peer();
                    return;
                } else {
                    std::string_view t(_input_data.data(),_input_data.size());
                    auto p = t.find("\r\n\r\n");
                    if (p != t.npos) {
                        if (websocket_handshake(t)) {
                            _input_data.clear();
                            data = data.substr(magic.size()-prev_size);
                            if (!data.empty()) {
                                receive_complete(data);
                            } else {
                                read_from_connection();
                            }
                            start_peer();
                        } else {
                            lost_connection();
                            return;
                        }
                    }
                    return;
                }
            } else {
                read_from_connection();
                return;
            }
            return;
        case Format::websocket:
            if (!parse_websocket_stream(data)) {
                lost_connection();
            }
            return;
        case Format::zbus:
            BridgeTCPCommon::receive_complete(data);
            return;
    }
}

void BridgeTCPServer::Peer::output_message(std::string_view message) {
    switch (_format) {
        default:
        case Format::unknown: return;   //drop message if unknown format;
        case Format::websocket:
                send_websocket_message(message);
            return;
        case Format::zbus:
                //format zbus protocol
            BridgeTCPCommon::output_message(message);
            return;
    }
}

static std::string_view split(std::string_view &data, std::string_view sep) {
    std::string_view r;
    auto pos = data.find(sep);
    if (pos == data.npos) {
        r = data;
        data = {};
    } else {
        r = data.substr(0,pos);
        data = data.substr(pos+sep.size());
    }
    return r;
}

static std::string_view trim(std::string_view data) {
    while (data.empty() && isspace(data.front())) data = data.substr(1);
    while (data.empty() && isspace(data.back())) data = data.substr(0, data.size()-1);
    return data;
}

static char fast_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    else return c;
}

static bool icmp(const std::string_view &a, const std::string_view &b) {
    if (a.size() != b.size()) return false;
    std::size_t cnt = a.size();
    for (std::size_t i = 0; i < cnt; ++i) {
        if (fast_tolower(a[i]) != fast_tolower(b[i])) return false;
    }
    return true;
}

std::string_view parse_websocket_header(std::string_view &data) {
    bool upgrade = false;
    bool connection = false;
    bool version = false;
    std::string_view key;

    auto first_line = split(data, "\r\n");
    if (!icmp(first_line, "get / http/1.1")) return {};
    while (!data.empty()) {
        auto value = split(data, "\r\n");
        auto key = split(value,":");
        key = trim(key);
        value = trim(value);
        if (icmp(key, "upgrade")) {
            if (!icmp(key, "websocket")) return {};
            upgrade = true;
        } else if (icmp(key, "connection")) {
            if (!icmp(key, "upgrade")) return {};
            connection = true;
        } else if (icmp(key, "sec-websocket-key:")) {
            key = value;
        } else if (icmp(key, "sec-websocket-version")) {
            int v;
            auto [_,ec] = std::from_chars(value.data(), value.data()+value.size(),v,10);
            if (ec != std::errc()) return {};
            if (v < 13) return {};
            version = true;
        }
    }
    if (!upgrade || !connection || !version) key = {};
    return key;

}

bool BridgeTCPServer::Peer::websocket_handshake(std::string_view &data) {
    auto key = parse_websocket_header(data);
    std::ostringstream resp;
    if (key.empty()) {
        resp << "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 0\r\n"
                "\r\n";
    } else {
        resp << "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: ";
        SHA1 sha1;
        sha1.update(key);
        sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        auto digest = sha1.final();
        std::string encoded;
        base64.encode(digest.begin(), digest.end(), std::back_inserter(encoded));
        resp << encoded << "\r\n\r\n";

    }
    _output_msg_sp.push_back(_output_data.size());
    auto v = resp.view();
    std::copy(v.begin(), v.end(), std::back_inserter(_output_data));
    flush_buffer();
    return !key.empty();
}

bool BridgeTCPServer::Peer::parse_websocket_stream(std::string_view data) {
    if (!_ws_parser.push_data(data)) return true;
    auto msg = _ws_parser.get_message();
    switch (msg.type) {
        default:
            _ws_parser.reset();
            return false;
        case ws::Type::connClose:
            send_websocket_message({{},msg.type,ws::Base::closeNormal});
            lost_connection();
            break;
        case ws::Type::ping:
            send_websocket_message({msg.payload,ws::Type::pong});
            break;
        case ws::Type::pong:
            break;
        case ws::Type::binary:
            dispatch_message(msg.payload);
            break;
    }
    _ws_parser.reset();
    return true;
}


void BridgeTCPServer::Peer::send_websocket_message(std::string_view data) {
    send_websocket_message({data, ws::Type::binary});
}

void BridgeTCPServer::Peer::send_websocket_message(const ws::Message &msg) {
    std::lock_guard _(_mx);
    _output_msg_sp.push_back(_output_data.size());
    ws::Builder bld(false);
    bld(msg, [&](char c){_output_data.push_back(c);});
    flush_buffer();
}

void BridgeTCPServer::Peer::start_peer() {
    if (_owner._auth_cfg.verify_fn) {
        Peer::request_auth(_owner._auth_cfg.digest_type);
    } else {
        initial_handshake();
    }
}

}
