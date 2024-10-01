#include "bridge_tcp_common.h"

namespace zerobus {


BridgeTCPCommon::BridgeTCPCommon(Bus bus, std::shared_ptr<INetContext> ctx,NetContextAux *aux)
:AbstractBinaryBridge(std::move(bus))
,_ctx(std::move(ctx))
,_aux(aux)

{
    read_from_connection();
    _ctx->callback_on_send_available(this);
}

BridgeTCPCommon::~BridgeTCPCommon() {
    _ctx->destroy(this);
}

void BridgeTCPCommon::on_send_available() {
    std::unique_lock lk(_mx);
    if (!_output_data.empty())  {
        auto s = _ctx->send(get_view_to_send(), this);
        if (s == 0) {
            lk.unlock();
            lost_connection();
            return;
        } else {
            if (after_send(s)) {
                _ctx->callback_on_send_available(this);
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

std::string_view BridgeTCPCommon::parse_messages(std::string_view data) {
    //process all messages until buffer is empty
    while (!data.empty()) {
        //determine required bytes to read size (we actually need +1)
        auto needsz = AbstractBinaryBridge::get_uint_size(data[0]) + 1;
        //check, whether data has required size
        if (needsz > data.size()) break;
        //if do, we can safely read size of message
        auto msgsize = AbstractBinaryBridge::read_uint(data);
        //check, whether remaining size is sufficient for message
        if (msgsize > data.size()) break;
        //is it? retrieve message part
        auto m = data.substr(0,msgsize);
        //remove the message from the data
        data = data.substr(msgsize);
        //dispatch message
        this->dispatch_message(m);
        //data can contain unprocessed messages
    }
    //data can contain incomplete message
    return data;

}

void BridgeTCPCommon::on_read_complete(std::string_view data) {
    if (data.empty()) {
        //function is called with empty string when disconnect happened
        lost_connection();
    } else {
        //if there is alread begin of incomplete message
        if (!_input_data.empty()) {
            //append data to the message
            std::copy(data.begin(), data.end(), std::back_inserter(_input_data));
            //and assume, that _input_data is buffer containing whole message
            data = std::string_view(_input_data.data(), _input_data.size());
        }
        //try to parse messages in data/buffer, returns incomplete message or empty data
        data = parse_messages(data);
        //check whether returned data is not the buffer itself
        //in this case, no extra processing is needed, otherwise we need copy
        //the incomplete message to the buffer
        if (data.data() != _input_data.data()) {
            //check and resize buffer if needed
            if (data.size() > _input_data.size()) _input_data.resize(data.size());
            //move, because data can be view of if _input_data, data to the buffer
            std::move(data.begin(), data.end(), _input_data.begin());
        }
        //resize input buffer to final size of the incomplete message
        _input_data.resize(data.size());
        //request read from network
        read_from_connection();
    }

}

NetContextAux* BridgeTCPCommon::get_context_aux() {
    return _aux;
}

void BridgeTCPCommon::read_from_connection() {
    _ctx->receive(_input_buffer, this);
}

void BridgeTCPCommon::on_auth_response(std::string_view , std::string_view , std::string_view ) {
    //empty - authentification must be implemented by child class
}

void BridgeTCPCommon::output_message(std::string_view data) {
    std::lock_guard _(_mx);
    _output_msg_sp.push_back(_output_data.size());
    AbstractBinaryBridge::write_string(std::back_inserter(_output_data), data);
    flush_buffer();
}

void BridgeTCPCommon::on_auth_request(std::string_view , std::string_view ) {
    //empty - authentification must be implemented by child class
}

void BridgeTCPCommon::on_welcome() {
    //empty - no needed
}

void BridgeTCPCommon::on_timeout() {
}

void BridgeTCPCommon::flush_buffer() {
    if (_output_allowed) {
        auto s = _ctx->send(get_view_to_send(), this);
        after_send(s);
        _output_allowed = false;
        _ctx->callback_on_send_available(this);
    }
}

}
