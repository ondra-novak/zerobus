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
	static constexpr unsigned int opcodeContFrame = 0;
	static constexpr unsigned int opcodeTextFrame = 1;
	static constexpr unsigned int opcodeBinaryFrame = 2;
	static constexpr unsigned int opcodeConnClose = 8;
	static constexpr unsigned int opcodePing = 9;
	static constexpr unsigned int opcodePong = 10;

	static constexpr std::uint16_t closeNormal = 1000;
	static constexpr std::uint16_t closeGoingAway = 1001;
	static constexpr std::uint16_t closeProtocolError = 1002;
	static constexpr std::uint16_t closeUnsupportedData = 1003;
	static constexpr std::uint16_t closeNoStatus = 1005;
	static constexpr std::uint16_t closeAbnormal = 1006;
	static constexpr std::uint16_t closeInvalidPayload = 1007;
	static constexpr std::uint16_t closePolicyViolation = 1008;
	static constexpr std::uint16_t closeMessageTooBig = 1009;
	static constexpr std::uint16_t closeMandatoryExtension = 1010;
	static constexpr std::uint16_t closeInternalServerError = 1011;
	static constexpr std::uint16_t closeTLSHandshake = 1015;

};

#ifndef CONSTEXPR_TESTABLE
#define CONSTEXPR_TESTABLE
#endif

class Parser: public Base {
public:

    ///Construct the parser
    /**
     * @param need_fragmented set true, to enable fragmented messages. This is
     * useful, if the reader requires to stream messages. Default is false,
     * when fragmented message is received, it is completed and returned as whole
     */
    CONSTEXPR_TESTABLE Parser(std::vector<char> &buffer, bool need_fragmented = false)
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
    CONSTEXPR_TESTABLE bool push_data(std::string_view data);

    ///Reset the internal state, discard current message
    CONSTEXPR_TESTABLE void reset();

    ///Retrieve parsed message
    /**
     * @return parsed message
     *
     * @note Message must be completed. If called while message is not yet complete result is UB.
     */
    CONSTEXPR_TESTABLE Message get_message() const;

    ///Retrieves true, if the message is complete
    /**
     * @retval false message is not complete yet, need more data
     * @retval true message is complete
     */
    CONSTEXPR_TESTABLE bool is_complete() const {
        return _state == State::complete;
    }


    ///When message is complete, some data can be unused, for example data of next message
    /**
     * Function returns unused data. If the message is not yet complete, returns
     * empty string
     * @return unused data
     */
    CONSTEXPR_TESTABLE std::string_view get_unused_data() const {
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
    CONSTEXPR_TESTABLE bool reset_parse_next() {
        auto tmp = get_unused_data();
        reset();
        return push_data(tmp);
    }



protected:
    enum class State {
        first_byte,
        second_byte,
        payload_len,
        masking,
        payload,
        complete
    };

    std::size_t _state_len = 0;
    std::size_t _payload_len = 0;
    int _mask_cntr = 0;


    std::vector<char> &_cur_message;
    bool _need_fragmented = false;
    bool _fin = false;
    bool _masked = false;

    State _state = State::first_byte;
    unsigned char _type = 0;
    char _masking[4] = {};
    std::string_view _unused_data;

    Type _final_type = Type::unknown;

    CONSTEXPR_TESTABLE  bool finalize();


    CONSTEXPR_TESTABLE  void reset_state();
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
    Builder(bool client);


    bool build(const Message &msg, std::vector<char> &output);
    bool build(const Message &msg, std::string &output);

protected:
    template<std::invocable<char> Fn>
    bool build_t(const Message &message, Fn &&output);

protected:
    bool _client = false;
    bool _fragmented = false;
    std::default_random_engine _rnd;
};

///calculate WebSocket Accept header value from key
/**
 * @param key content of Key header
 * @return content of Accept header
 */
std::string calculate_ws_accept(std::string_view key);

///Generates random Key header value
std::string generate_ws_key();


}

}
