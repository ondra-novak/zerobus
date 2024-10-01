#pragma once


#include <random>

#include <string_view>
#include <vector>


namespace zerobus {

namespace ws {

enum class Type: std::uint8_t {
    unknown,
    ///text frame
    text,
    ///binary frame
    binary,
    ///connection close frame
    connClose,
    ///ping frame
    ping,
    ///pong frame
    pong,
};

struct Message {
    ///contains arbitrary message payload
    /** Data are often stored in the context of the parser and remains valid
     * until next message is requested and parsed.
     */
    std::string_view payload;
    ///type of the frame
    Type type = Type::text;
    ///contains code associated with message connClose
    /** This field is valid only for connClose type */
    std::uint16_t code = 0;
    ///contains true, if this was the last packet of fragmented message
    /**
     * Fragmented messages are identified by fin = false. The type is always
     * set for correct message type (so there is no continuation type on the
     * frontend). If you receive false for the fin, you need to read next message
     * and combine data to one large message.
     *
     * The parser also allows to combine fragmented messages by its own. If this
     * option is turned on, this flag is always set to true
     */
    bool fin = true;
};

///Some constants defined for websockets
struct Base {
public:
	static const unsigned int opcodeContFrame = 0;
	static const unsigned int opcodeTextFrame = 1;
	static const unsigned int opcodeBinaryFrame = 2;
	static const unsigned int opcodeConnClose = 8;
	static const unsigned int opcodePing = 9;
	static const unsigned int opcodePong = 10;

	static const unsigned int closeNormal = 1000;
	static const unsigned int closeGoingAway = 1001;
	static const unsigned int closeProtocolError = 1002;
	static const unsigned int closeUnsupportedData = 1003;
	static const unsigned int closeNoStatus = 1005;
	static const unsigned int closeAbnormal = 1006;
	static const unsigned int closeInvalidPayload = 1007;
	static const unsigned int closePolicyViolation = 1008;
	static const unsigned int closeMessageTooBig = 1009;
	static const unsigned int closeMandatoryExtension = 1010;
	static const unsigned int closeInternalServerError = 1011;
	static const unsigned int closeTLSHandshake = 1015;

};


class Parser: public Base {
public:

    ///Construct the parser
    /**
     * @param need_fragmented set true, to enable fragmented messages. This is
     * useful, if the reader requires to stream messages. Default is false,
     * when fragmented message is received, it is completed and returned as whole
     */
    Parser(std::vector<char> &buffer, bool need_fragmented = false)
        :_cur_message(buffer)
        ,_need_fragmented(need_fragmented) {}


    ///push data to the parser
    /**
     * @param data data pushed to the parser
     * @retval false data processed, but more data are needed
     * @retval true data processed and message is complete. You can use
     * interface to retrieve information about the message. To parse
     * next message, you need to call reset()
     */
    bool push_data(std::string_view data);

    ///Reset the internal state, discard current message
    void reset();

    ///Retrieve parsed message
    /**
     * @return parsed message
     *
     * @note Message must be completed. If called while message is not yet complete result is UB.
     */
    Message get_message() const;

    ///Retrieves true, if the message is complete
    /**
     * @retval false message is not complete yet, need more data
     * @retval true message is complete
     */
    bool is_complete() const {
        return _state == State::complete;
    }


    ///When message is complete, some data can be unused, for example data of next message
    /**
     * Function returns unused data. If the message is not yet complete, returns
     * empty string
     * @return unused data
     */
    std::string_view get_unused_data() const {
        return _unused_data;
    }

    ///Reset internal state and use unused data to parse next message
    /**
     * @retval true unused data was used to parse next message
     * @retval false no enough unused data, need more data to parse message,
     * call push_data() to add more data
     *
     * @note the function performs following code
     *
     * @code
     * auto tmp = get_unused_data();
     * reset();
     * return push_data(tmp);
     * @endcode
     */
    bool reset_parse_next() {
        auto tmp = get_unused_data();
        reset();
        return push_data(tmp);
    }



protected:
    //contains next state
    enum class State: std::uint8_t {
        first_byte,
        payload_len,
        payload_len7,
        payload_len6,
        payload_len5,
        payload_len4,
        payload_len3,
        payload_len2,
        payload_len1,
        payload_len0,
        masking,
        masking1,
        masking2,
        masking3,
        masking4,
        payload_begin,
        payload,
        complete
    };

    std::vector<char> &_cur_message;
    bool _need_fragmented = false;
    bool _fin = false;
    bool _masked = false;

    State _state = State::first_byte;
    unsigned char _type = 0;
    std::uint64_t _payload_len = 0;
    char _masking[4] = {};
    std::string_view _unused_data;

    Type _final_type = Type::unknown;

    bool finalize();


    void reset_state();
};

///Builder builds Websocket frames
/**
 * Builder can be used as callable, which accepts Message and returns binary
 * representation of the frame.
 *
 * Note that object is statefull. This is important if you sending fragmented messages,
 * the object tracks state and correctly reports type of continuation frames
 *
 *
 */
class Builder: public Base {
public:

    ///Construct the builder
    /**
     * @param client set true if the builder generates client frames. Otherwise
     * set false (for server)
     */
    Builder(bool client)
        :_client(client) {
        if (_client) {
            std::random_device dev;
            _rnd.seed(dev());
        }
    }

    ///Build frame
    /**
     * @param message message to build.
     * @retval true success
     * @retval false invalid message
     *
     * @note To send fragmented message, you need correctly use _fin flag on
     * the message. Fragmented message must have _fin = false for all
     * fragments expect the last. The last fragment must have _fin = true; Type
     * of the message is retrieved from the first fragment and it is ignored on
     * other fragments.
     */
    template<std::invocable<char> Fn>
    bool operator()(const Message &message, Fn &&output) {
        std::string tmp;
        std::string_view payload = message.payload;

        if (message.type == Type::connClose) {
            tmp.push_back(static_cast<char>(message.code>>8));
            tmp.push_back(static_cast<char>(message.code & 0xFF));
            if (!message.payload.empty()) {
                std::copy(message.payload.begin(), message.payload.end(), std::back_inserter(tmp));
            }
            payload = {tmp.c_str(), tmp.length()+1};
        }

        // opcode and FIN bit
        char opcode = opcodeContFrame;
        bool fin = message.fin;
        if (!_fragmented) {
            switch (message.type) {
                default:
                case Type::unknown: return false;
                case Type::text: opcode = opcodeTextFrame;break;
                case Type::binary: opcode = opcodeBinaryFrame;break;
                case Type::ping: opcode = opcodePing;break;
                case Type::pong: opcode = opcodePong;break;
                case Type::connClose: opcode = opcodeConnClose;break;
            }
        }
        _fragmented = !fin;
        output((fin << 7) | opcode);
        // payload length
        std::uint64_t len = payload.size();

        char mm = _client?0x80:0;
        if (len < 126) {
            output(mm| static_cast<char>(len));
        } else if (len < 65536) {
            output(mm | 126);
            output(static_cast<char>((len >> 8) & 0xFF));
            output(static_cast<char>(len & 0xFF));
        } else {
            output(mm | 127);
            output(static_cast<char>((len >> 56) & 0xFF));
            output(static_cast<char>((len >> 48) & 0xFF));
            output(static_cast<char>((len >> 40) & 0xFF));
            output(static_cast<char>((len >> 32) & 0xFF));
            output(static_cast<char>((len >> 24) & 0xFF));
            output(static_cast<char>((len >> 16) & 0xFF));
            output(static_cast<char>((len >> 8) & 0xFF));
            output(static_cast<char>(len & 0xFF));
        }
        char masking_key[4];

        if (_client) {
            std::uniform_int_distribution<> dist(0, 255);

            for (int i = 0; i < 4; ++i) {
                masking_key[i] = dist(_rnd);
                output(masking_key[i]);
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                masking_key[i] = 0;
            }
        }

        int idx =0;
        for (char c: payload) {
            c ^= masking_key[idx];
            idx = (idx + 1) & 0x3;
            output(c);
        }
        return true;

    }


protected:
    bool _client = false;
    bool _fragmented = false;
    std::default_random_engine _rnd;
};
}

}
