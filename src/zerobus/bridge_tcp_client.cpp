#include "bridge_tcp_client.h"

namespace zerobus {


BridgeTCPClient::BridgeTCPClient(Bus bus, std::shared_ptr<INetContext> ctx, std::string address)
:BridgeTCPClient(std::move(bus)) {
    bind(std::move(ctx), std::move(address));
}


BridgeTCPClient::BridgeTCPClient(Bus bus, std::string address)
:BridgeTCPClient(std::move(bus), make_network_context(1), std::move(address)) {

}

BridgeTCPClient::BridgeTCPClient(Bus bus)
:BridgeTCPCommon(std::move(bus), false)
,_session_id(generate_session_id())
{

}

BridgeTCPClient::~BridgeTCPClient() {
    unregister_monitor(this);
    output_message(ws::Message{"", ws::Type::connClose, _ws_builder.closeNormal});
    if (_linger_timeout) {
        std::unique_lock lk(_mx);
        _hwm = 0;
        _hwm_timeout = _linger_timeout;
        block_hwm(lk);
    }
    destroy();
}

void BridgeTCPClient::bind(std::shared_ptr<INetContext> ctx, std::string address) {
    BridgeTCPCommon::bind(ctx, ctx->peer_connect(get_address_from_url(address)));
    _address = address;
    register_monitor(this);
   _ctx->ready_to_send(_aux, this);

}

void BridgeTCPClient::set_linger_timeout(std::size_t timeout_ms) {
    _linger_timeout = timeout_ms;
}


void BridgeTCPClient::on_timeout() noexcept {
    if (_timeout_reconnect) {
        _timeout_reconnect = false;
        lost_connection();
    } else {
        BridgeTCPCommon::on_timeout();
        send_mine_channels();
    }
}

void BridgeTCPClient::close() {
    BridgeTCPClient::lost_connection();
}

void BridgeTCPClient::lost_connection() {
    try {
        std::lock_guard _(_mx);
        this->_output_allowed = false;
        _ctx->reconnect(_aux, get_address_from_url(_address));
        _output_cursor = 0; //last output incomplete message will be send again
        _handshake = true;
        _ctx->ready_to_send(_aux, this);
    } catch (...) {
        _timeout_reconnect = true;
        _ctx->set_timeout(_aux, std::chrono::system_clock::now()+std::chrono::seconds(2), this);
    }

}

void BridgeTCPClient::on_channels_update() noexcept {
    _ctx->set_timeout(_aux, std::chrono::system_clock::time_point::min(), this);
}


void BridgeTCPClient::clear_to_send() noexcept {
    if (_handshake) {
        if (!send_handshake()) {
          //magic should fit to the buffer whole
            lost_connection();
        } else {
            read_from_connection();
        }

    } else {
        BridgeTCPCommon::clear_to_send();
    }

}


bool BridgeTCPClient::send_handshake() {
    std::string key = ws::generate_ws_key();
    _expected_ws_accept = ws::calculate_ws_accept(key);
    std::string path = get_path_from_url(_address);
    if (path.empty() || path.back() != '/') path.push_back('/');
    path.append(_session_id);

    std::ostringstream hdr;
    hdr << "GET " << path << " HTTP/1.1\r\n"
           "Host: " << get_address_from_url(_address) << "\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: " << key << "\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "\r\n";

    auto v = hdr.view();
    return _ctx->send(_aux, v) == v.size();

}
void BridgeTCPClient::receive_complete(std::string_view data) noexcept {
    if (_handshake) {
        if (data.empty()) {
            lost_connection();
            return;
        }
        std::copy(data.begin(), data.end(), std::back_inserter(_input_data));
        std::string_view whole_hdr(_input_data.begin(), _input_data.end());
        auto pos = whole_hdr.find("\r\n\r\n");
        if (pos == whole_hdr.npos) return;
        std::string rest(whole_hdr.substr(pos+4));
        whole_hdr = whole_hdr.substr(0,pos);
        _input_data.clear();
        if (check_ws_response(whole_hdr)) {
            _handshake = false;
            _ctx->ready_to_send(_aux, this);
            if (rest.empty()) {
                read_from_connection();
            } else {
                BridgeTCPCommon::receive_complete(rest);
            }
            send(ChannelReset{});
        } else {
            lost_connection();
        }
    } else {
        BridgeTCPCommon::receive_complete(data);
    }
}

bool BridgeTCPClient::check_ws_response(std::string_view hdr) {
    bool upgrade = false;
    bool connection = false;
    bool accept = false;
    auto first_line = parse_header(hdr, [&](std::string_view key, std::string_view value){
        if (icmp(key, "upgrade")) {
            if (icmp(value, "websocket")) upgrade = true;
        } else if (icmp(key, "connection")) {
            if (icmp(value, "upgrade")) connection = true;
        } else if (icmp(key, "sec-websocket-accept")) {
            if (value == _expected_ws_accept) accept = true;
        }
    });
    return icmp(first_line,"http/1.1 101 switching protocols")
            && upgrade && connection && accept;
}

constexpr std::string_view valid_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_";


std::string BridgeTCPClient::generate_session_id() {
    std::string s;
    s.resize(32);
    std::random_device rnd;
    std::uniform_int_distribution<unsigned int> dist(0,static_cast<unsigned int>(valid_chars.size()-1));
    for (auto &c: s) c = valid_chars[dist(rnd)];
    return s;
}
}

