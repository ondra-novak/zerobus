#include "websocket.h"

#include <random>


namespace zerobus {

namespace ws {

bool Parser::push_data(std::string_view data) {
    std::size_t sz = data.size();
    for (std::size_t i = 0; i < sz; i++) {
        char c = data[i];
        switch (_state) {
            case State::first_byte:
                _fin = (c & 0x80) != 0;
                _type = c & 0xF;
                _state = State::payload_len;
                break;
            case State::payload_len:
                _masked = (c & 0x80) != 0;
                c &= 0x7F;
                if (c == 127) _state = State::payload_len7;
                else if (c == 126) _state = State::payload_len1;
                else {
                    _payload_len = c;
                    _state = State::masking;
                }
                break;
            case State::payload_len0:
            case State::payload_len1:
            case State::payload_len2:
            case State::payload_len3:
            case State::payload_len4:
            case State::payload_len5:
            case State::payload_len6:
            case State::payload_len7:
                _payload_len = (_payload_len<<8) + static_cast<unsigned char>(c);
                _state = static_cast<State>(static_cast<std::underlying_type_t<State> >(_state)+1);
                break;
            case State::masking:
                _state = _masked?State::masking1:State::payload;
                --i; //retry this byte
                break;
            case State::masking1:
            case State::masking2:
            case State::masking3:
            case State::masking4:
                _masking[static_cast<int>(_state) - static_cast<int>(State::masking1)] = c;
                _state = static_cast<State>(static_cast<std::underlying_type_t<State> >(_state)+1);
                break;
            case State::payload_begin:
                _state = _payload_len?State::payload:State::complete;
                --i;
                break;
            case State::payload:
                _cur_message.push_back(c ^ _masking[_cur_message.size() & 0x3]);
                _state = _cur_message.size() == _payload_len?State::complete:State::payload;
                break;
            case State::complete:
                _unused_data = data.substr(i);
                return finalize();
        }
    }
    if (_state >= State::payload_begin &&  _cur_message.size() == _payload_len) {
        return finalize();
    }
    return false;
}

void Parser::reset_state() {
    _state = State::first_byte;
    for (int i = 0; i < 4; ++i) _masking[i] = 0;
    _fin = false;
    _masked = false;
    _payload_len = 0;
    _unused_data = {};
}

void Parser::reset() {
    reset_state();
    _cur_message.clear();
}

Message Parser::get_message() const {
    if (_final_type == Type::connClose) {
        std::uint16_t code = 0;
        std::string_view message;
        if (_cur_message.size() >= 2) {
            code = static_cast<unsigned char>(_cur_message[0]) * 256 + static_cast<unsigned char>(_cur_message[1]);
        }
        if (_cur_message.size() > 2) {
            message = std::string_view(_cur_message.data()+2, _cur_message.size() - 3);
        }
        return Message {
            message,
            _final_type,
            code,
            _fin
        };
    } else {
        _cur_message.push_back('\0');
        _cur_message.pop_back();
        return Message {
            {_cur_message.data(), _cur_message.size()},
            _final_type,
            _type,
            _fin
        };
    }
}



bool Parser::finalize() {
    switch (_type) {
        case opcodeContFrame: break;
        case opcodeConnClose: _final_type = Type::connClose; break;
        case opcodeBinaryFrame:  _final_type = Type::binary;break;
        case opcodeTextFrame:  _final_type = Type::text;break;
        case opcodePing:  _final_type = Type::ping;break;
        case opcodePong:  _final_type = Type::pong;break;
        default: _final_type = Type::unknown;break;
    }

    if (!_fin) {
        if (_need_fragmented) {
            auto tmp = _unused_data;
            reset_state();
            return push_data(tmp);
        }
    }
    return true;
}

}

}


