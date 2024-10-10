#include "bridge_tcp_common.h"
#include <random>
#include <variant>
namespace zerobus {


thread_local Serialization BridgeTCPCommon::_ser = {};

BridgeTCPCommon::BridgeTCPCommon(Bus bus, std::shared_ptr<INetContext> ctx,ConnHandle aux, bool client)
:AbstractBridge(std::move(bus))
,_ctx(std::move(ctx))
,_aux(aux)
,_ws_builder(client)
,_ws_parser(_input_data, false)

{
}

void BridgeTCPCommon::init() {
    read_from_connection();
    _ctx->ready_to_send(_aux, this);

}

BridgeTCPCommon::~BridgeTCPCommon() {
    _ctx->destroy(_aux);
}

void BridgeTCPCommon::clear_to_send() noexcept {
    std::unique_lock lk(_mx);
    if (!_output_data.empty())  {
        auto s = _ctx->send(_aux, get_view_to_send());
        if (s == 0) {
            lk.unlock();
            lost_connection();
            return;
        } else {
            if (after_send(s)) {
                _ctx->ready_to_send(_aux, this);
                return;
            }
        }
    }
    _output_allowed = true;
}



bool BridgeTCPCommon::after_send(std::size_t sz) {
    // - _output_msg_sp contain message seaprators, indices to _ouput_data where messages begins
    // - if this buffer is empty, this is invalid operation, so return false (nothing to do)
    if (_output_msg_sp.empty()) return false;

    // increase position of output cursor
    _output_cursor += sz;
    // if we reached end, writing is done
    if (_output_cursor == _output_data.size()) {
        //reset cursor
        _output_cursor = 0;
        //clear separators
        _output_msg_sp.clear();
        //clear data
        _output_data.clear();
        //no more write is needed
        return false;
    }

    //so some data not been written
    //find index which messages has been send complete
    //find index which is greater then cursor (end of next message)
    //this can return end (last message)
    auto iter = std::upper_bound(_output_msg_sp.begin(), _output_msg_sp.end(), _output_cursor);
    //decrease iterator, so we pointing to offset of incomplete message
    --iter;
    //retrieve its offset
    auto pos = *iter;
    //if nonzero offset
    if (pos) {
        auto t = _output_msg_sp.begin();
        //erase and shift while recalculating separators removing complete message
        //the first separator is always zero
        while (iter != _output_msg_sp.end()) {
            *t = *iter - pos;
            ++iter;
            ++t;
        }
        //erase anything left
        _output_msg_sp.erase(t, _output_msg_sp.end());
        //erase complete messages
        _output_data.erase(_output_data.begin(), _output_data.begin()+pos);
        //update cursor to point to current message and already send offset
        _output_cursor-=pos;
    }
    return true; //we still need continue in sending
}

std::string_view BridgeTCPCommon::get_view_to_send() const {
    //retrieves output data with offset of _output_cursor
    return std::string_view(_output_data.data()+_output_cursor, _output_data.size()-_output_cursor);
}

void BridgeTCPCommon::deserialize_message(const std::string_view &msg) {
    std::visit([&](auto &&m){
        this->receive(std::move(m));
    }, _deser(msg, _ptr.get()));
}

void BridgeTCPCommon::receive_complete(std::string_view data) noexcept {
    if (data.empty()) {
        //function is called with empty string when disconnect happened
        lost_connection();
    } else {
        while (_ws_parser.push_data(data)) {
            ws::Message msg = _ws_parser.get_message();
            switch (msg.type) {
                case ws::Type::binary:
                    deserialize_message(msg.payload);
                    break;
                case ws::Type::ping:
                    output_message(ws::Message{msg.payload, ws::Type::pong});
                    break;
                case ws::Type::pong:
                    break;
                case ws::Type::connClose:
                    output_message(ws::Message{"", ws::Type::connClose, _ws_builder.closeNormal});
                    _ws_parser.reset();
                    lost_connection();
                    return;
                default:    //ignore unknown message
                    break;
            }
            data = _ws_parser.get_unused_data();
            _ws_parser.reset();
        }
        //request read from network
        read_from_connection();
    }

}


void BridgeTCPCommon::read_from_connection() {
    _ctx->receive(_aux, _input_buffer, this);
}


void BridgeTCPCommon::output_message(const ws::Message &msg) {
    std::lock_guard _(_mx);
    if (_handshake) return; //can't send message when handshake
    if (_output_data.size() > _hwm) return ;    //TODO: try to slow down when buffer reaches HWM
    _output_msg_sp.push_back(_output_data.size());
    _ws_builder.build(msg, _output_data);
    flush_buffer();

}
void BridgeTCPCommon::output_message(std::string_view data) {
    output_message({data, ws::Type::binary});
}


void BridgeTCPCommon::on_timeout() noexcept {
}

void BridgeTCPCommon::flush_buffer() {
    if (_output_allowed) {
        auto s = _ctx->send(_aux, get_view_to_send());
        after_send(s);
        _output_allowed = false;
        _ctx->ready_to_send(_aux, this);
    }
}


std::string_view BridgeTCPCommon::split(std::string_view &data, std::string_view sep) {
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

std::string_view BridgeTCPCommon::trim(std::string_view data) {
    while (!data.empty() && isspace(data.front())) data = data.substr(1);
    while (!data.empty() && isspace(data.back())) data = data.substr(0, data.size()-1);
    return data;
}

char BridgeTCPCommon::fast_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    else return c;
}

bool BridgeTCPCommon::icmp(const std::string_view &a, const std::string_view &b) {
    if (a.size() != b.size()) return false;
    std::size_t cnt = a.size();
    for (std::size_t i = 0; i < cnt; ++i) {
        if (fast_tolower(a[i]) != fast_tolower(b[i])) return false;
    }
    return true;
}

std::string BridgeTCPCommon::get_address_from_url(std::string_view url) {
    if (url.substr(0, 5) != "ws://") return std::string(url);
    url = url.substr(5);
    auto pos = url.find('/');
    auto addr = url.substr(0, pos);
    pos = addr.find(':');
    if (pos == addr.npos) {
        return std::string(addr).append(":80");
    } else {
        return std::string(addr);
    }
}

std::string BridgeTCPCommon::get_path_from_url(std::string_view url) {
    if (url.substr(0, 5) != "ws://") return "/";
    url = url.substr(5);
    auto pos = url.find('/');
    if (pos == url.npos) return "/";
    return std::string(url.substr(pos));

}

void BridgeTCPCommon::set_hwm(std::size_t hwm) {
    std::lock_guard _(_mx);
    _hwm = hwm;
}

void BridgeTCPCommon::send(ChannelReset&& m) noexcept {
    output_message(_ser(m));
}

void BridgeTCPCommon::send(CloseGroup&& m) noexcept {
    output_message(_ser(m));
}

void BridgeTCPCommon::send(Message &&m) noexcept {
    output_message(_ser(m));
}

void BridgeTCPCommon::send(ChannelUpdate &&m) noexcept {
    output_message(_ser(m));
}

void BridgeTCPCommon::send(ClearPath&&m) noexcept {
    output_message(_ser(m));
}

void BridgeTCPCommon::send(AddToGroup&&m) noexcept {
    output_message(_ser(m));
}

}
