#include "bridge_tcp_server.h"

#include "bridge.h"
#include "sha1.h"
#include "base64.h"
#include <charconv>

namespace zerobus {

BridgeTCPServer::BridgeTCPServer(Bus bus, std::shared_ptr<INetContext> ctx, std::string address_port, AuthConfig auth_cfg)
        :_bus(bus),_ctx(std::move(ctx)),_auth_cfg(std::move(auth_cfg))
        , _path(BridgeTCPCommon::get_path_from_url(address_port))
        ,_custom_page([this](std::string_view uri)->CustomPage {
            if (uri == _path) {
                return {400,"Bad request", "text/plain", "Please, use websocket connection"};
            } else {
                return {404,"Not found", "text/html", "<html><body><h1>404 Not found</h1></body></html>"};
            }
        })
        {
        _aux = _ctx->create_server(BridgeTCPCommon::get_address_from_url(address_port));
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
    std::lock_guard _(_mx);
    _send_mine_channels_flag = true;
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
        if (_send_mine_channels_flag) {
            _send_mine_channels_flag = true;
            for (const auto &x: _peers) {
                if (!x->disabled()) {
                    x->send_mine_channels();
                }
            }
        }
        if (_lost_peers_flag) {
            _peers.erase(std::remove_if(_peers.begin(), _peers.end(), [&](const auto &peer){return peer->is_lost();}), _peers.end());
            _lost_peers_flag = false;
        }
    }
    _ctx->set_timeout(_aux, _next_ping, this);
}

BridgeTCPServer::Peer::Peer(BridgeTCPServer &owner, ConnHandle aux, unsigned int id)
    :BridgeTCPCommon(owner._bus, owner._ctx, aux, false)
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
        BridgeTCPCommon::output_message(ws::Message{"",ws::Type::ping});
        _ping_sent = true;
    } else {
        _ping_sent = false;
    }
    _activity_check = true;
    return false;
}

void BridgeTCPServer::Peer::lost_connection() {
    _lost = true;
    _owner.lost_connection();
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

void BridgeTCPServer::set_custom_page_callback(
        std::function<CustomPage(std::string_view)> cb) {
    _custom_page = std::move(cb);
}

void BridgeTCPServer::lost_connection() {
    std::lock_guard _(_mx);
    _lost_peers_flag = true;
    _ctx->set_timeout(_aux, std::chrono::system_clock::time_point::min(), this);
}

void BridgeTCPServer::Peer::receive_complete(std::string_view data) noexcept {
    _activity_check = false;
    if (_handshake) {
        if (data.empty()) {
            lost_connection();
            return;
        }
        std::copy(data.begin(), data.end(), std::back_inserter(_input_data));
        std::string_view t(_input_data.data(),_input_data.size());
        auto p = t.find("\r\n\r\n");
        if (p != t.npos) {
            if (websocket_handshake(t)) {
                _input_data.clear();
                read_from_connection();
                start_peer();
            } else {
                lost_connection();
                return;
            }

        }
    } else {
        BridgeTCPCommon::receive_complete(data);
    }
}



BridgeTCPServer::Peer::ParseResult BridgeTCPServer::Peer::parse_websocket_header(std::string_view data) {
    bool upgrade = false;
    bool connection = false;
    bool version = false;
    std::string_view wskey;

    auto first_line = parse_header(data, [&](auto key, auto value){
        if (icmp(key, "upgrade")) {
            if (icmp(value, "websocket")) upgrade = true;
        } else if (icmp(key, "connection")) {
            if (icmp(value, "upgrade")) connection = true;
        } else if (icmp(key, "sec-websocket-key")) {
            wskey = value;
        } else if (icmp(key, "sec-websocket-version")) {
            int v;
            auto [_,ec] = std::from_chars(value.data(), value.data()+value.size(),v,10);
            if (ec == std::errc() && v >= 13) version = true;
        }
    });
    auto method = split(first_line, " ");
    auto path = split(first_line, " ");
    auto protocol = split(first_line, " ");
    bool ok = icmp(method, "get")
            && path == _owner._path
            && icmp(protocol,"http/1.1")
            && upgrade && connection && version;
    if (!ok) wskey= {};
    return {wskey, path, method};

}


bool BridgeTCPServer::Peer::websocket_handshake(std::string_view &data) {
    auto rs = parse_websocket_header(data);
    std::ostringstream resp;
    if (rs.key.empty()) {
        if (icmp(rs.method, "GET")) {
            auto cp = _owner._custom_page(rs.uri);

            resp << "HTTP/1.1 "<<cp.status_code << " " << cp.status_message << "\r\n"
                    "Server: zerobus\r\n"
                    "Connection: close\r\n"
                    "Content-Type: " << cp.content_type << "\r\n"
                    "Content-Length: " << cp.content.size() << "\r\n"
                    "\r\n" << cp.content;

        } else {
            resp << "HTTP/1.1 405 Method not allowed\r\n"
                    "Allow: GET\r\n"
                    "Server: zerobus\r\n"
                    "Connection: close\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n";
        }
    } else {
        resp << "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Server: zerobus\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: ";
        resp << calculate_ws_accept(rs.key) << "\r\n\r\n";

    }
    _output_msg_sp.push_back(_output_data.size());
    auto v = resp.view();
    std::copy(v.begin(), v.end(), std::back_inserter(_output_data));
    flush_buffer();
    return !rs.key.empty();
}


void BridgeTCPServer::Peer::start_peer() {
    _handshake = false;
    if (_owner._auth_cfg.verify_fn) {
        Peer::request_auth(_owner._auth_cfg.digest_type);
    } else {
        initial_handshake();
    }
}

}
