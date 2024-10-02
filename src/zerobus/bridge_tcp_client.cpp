#include "bridge_tcp_client.h"

namespace zerobus {


BridgeTCPClient::BridgeTCPClient(Bus bus, std::shared_ptr<INetContext> ctx, std::string address, AuthCallback acb)
:BridgeTCPCommon(std::move(bus), ctx, ctx->peer_connect(address))
,_address(std::move(address))
,_acb(std::move(acb))
{
    register_monitor(this);
    init();
}

BridgeTCPClient::BridgeTCPClient(Bus bus, std::string address, AuthCallback acb)
:BridgeTCPClient(std::move(bus), make_context(1), std::move(address), std::move(acb)) {

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
        _ctx->reconnect(_aux, _address);
        _input_data.clear();  //any incomplete message is lost
        _output_cursor = 0; //last output incomplete message will be send again
        read_from_connection(); //start reading
        _ctx->ready_to_send(_aux, this); //generate signal to write
    } catch (...) {
        _timeout_reconnect = true;
        _ctx->set_timeout(_aux, std::chrono::system_clock::now()+std::chrono::seconds(2), this);
    }

}

void BridgeTCPClient::on_channels_update() noexcept {
    _ctx->set_timeout(_aux, std::chrono::system_clock::now(), this);
}

void BridgeTCPClient::on_auth_request(std::string_view proof_type, std::string_view salt) {
    if (_acb) {
        _acb(AuthRequest{proof_type, salt}, [this](AuthResponse r){
            send_auth_response(r.ident, r.proof);
        });
    }
}

void BridgeTCPClient::clear_to_send() noexcept {
    if (_handshake) {
        if (_ctx->send(_aux, magic) < magic.size()) {   //magic should fit to the buffer whole
            lost_connection();
        } else {
            _handshake = false;
            _ctx->ready_to_send(_aux, this);
        }
    } else {
        BridgeTCPCommon::clear_to_send();
    }
}

}
