#include "bridge_tcp_client.h"

namespace zerobus {


BridgeTCPClient::BridgeTCPClient(Bus bus, std::shared_ptr<INetContext> ctx, std::string address)
:BridgeTCPCommon(std::move(bus), ctx, ctx->peer_connect(get_address_from_url(address)), true)
,_address(std::move(address))
{
    register_monitor(this);
    init();
}

BridgeTCPClient::BridgeTCPClient(Bus bus, std::string address)
:BridgeTCPClient(std::move(bus), make_context(1), std::move(address)) {

}

BridgeTCPClient::~BridgeTCPClient() {
    unregister_monitor(this);
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


void BridgeTCPClient::lost_connection() {
    try {
        std::lock_guard _(_mx);
        this->_output_allowed = false;
        _ctx->reconnect(_aux, get_address_from_url(_address));
        _output_cursor = 0; //last output incomplete message will be send again
        _handshake = true;
        BridgeTCPCommon::init();
    } catch (...) {
        _timeout_reconnect = true;
        _ctx->set_timeout(_aux, std::chrono::system_clock::now()+std::chrono::seconds(2), this);
    }

}

void BridgeTCPClient::on_channels_update() noexcept {
    _ctx->set_timeout(_aux, std::chrono::system_clock::now(), this);
}


void BridgeTCPClient::clear_to_send() noexcept {
    if (_handshake) {
        if (!send_handshake()) {
          //magic should fit to the buffer whole
            lost_connection();
        }
    } else {
        BridgeTCPCommon::clear_to_send();
    }
}


bool BridgeTCPClient::send_handshake() {
    std::string key = generate_ws_key();
    _expected_ws_accept = calculate_ws_accept(key);

    std::ostringstream hdr;
    hdr << "GET " << get_path_from_url(_address) << " HTTP/1.1\r\n"
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



}
