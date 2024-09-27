#include "bridge_tcp_client.h"

namespace zerobus {

std::shared_ptr<BridgeTCPClient> BridgeTCPClient::connect(Bus bus,
                std::shared_ptr<INetContext> ctx, std::string address) {
    auto aux = ctx->peer_connect(address);
    return std::make_shared<BridgeTCPClient>(bus, std::move(ctx), aux, std::move(address));
}

BridgeTCPClient::BridgeTCPClient(Bus bus, std::shared_ptr<INetContext> ctx,
        NetContextAux *aux, std::string address)
:_bridge(std::move(bus), [this](std::string_view data){
    output_fn(data);
}),_ctx(std::move(ctx))
,_aux(aux)
,_address(address)

{
    begin_read();
    _ctx->callback_on_send_available(this);
}

void BridgeTCPClient::on_send_available() {
    std::lock_guard _(_mx);
    if (!_output_buff.empty())  {
        auto s = _ctx->send(std::string_view(_output_buff.data(), _output_buff.size()), this);
        if (s == 0) {
            reconnect();
            return;
        } else {
            _output_buff.erase(_output_buff.begin(),_output_buff.begin()+s);
            if (!_output_buff.empty()) {
                _ctx->callback_on_send_available(this);
                return;
            }
        }
    }
    _output_allowed = true;
}

void BridgeTCPClient::output_fn(std::string_view data) {
    std::lock_guard _(_mx);
    BinaryBridge::write_string(std::back_inserter(_output_buff), data);
    if (_output_allowed) {
        auto s = _ctx->send(std::string_view(_output_buff.data(), _output_buff.size()), this);
        _output_buff.erase(_output_buff.begin(),_output_buff.begin()+s);
        if (!_output_buff.empty()) {
            _output_allowed = false;
            _ctx->callback_on_send_available(this);
        }
    }
}

void BridgeTCPClient::on_timeout() {
    if (_timeout_reconnect) {
        _timeout_reconnect = false;
        reconnect();
    }
}

void BridgeTCPClient::reconnect() {
    try {
        _ctx->reconnect(this, _address);
        begin_read();
        _ctx->callback_on_send_available(this);
    } catch (...) {
        _timeout_reconnect = true;
        _ctx->set_timeout(std::chrono::system_clock::now()+std::chrono::seconds(2), this);
    }

}

void BridgeTCPClient::on_read_complete(std::string_view data) {
    if (data.empty()) {
        reconnect();
    } else {
        std::copy(data.begin(), data.end(), std::back_inserter(_input_buff));
        begin_read();
        std::string_view msgtext(_input_buff.data(), _input_buff.size());
        auto sz = BinaryBridge::read_uint(msgtext);
        if (sz > msgtext.size()) return;
        auto m = msgtext.substr(0,sz);
        auto rm = msgtext.substr(sz);
        _bridge.dispatch_message(m);
        std::copy(rm.begin(), rm.end(), _input_buff.begin());
        _input_buff.resize(rm.size());
    }

}

NetContextAux* BridgeTCPClient::get_context_aux() {
    return _aux;
}

void BridgeTCPClient::begin_read() {
    _ctx->receive(_input_buffer, this);
}

}
