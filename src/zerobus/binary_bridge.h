#include "bridge.h"
#include <functional>

namespace zerobus {

class AbstractBinaryBridge: public AbstractBridge {
public:

    enum class MessageType: char {
        ///a message
        message = 0,
        ///list of channels
        channels = 1,
        ///ping message
        /** sent by any side if it wants to know whether other side is alive */
        ping = 2,
        ///pong message
        /** sent by any side as response to ping message. The message itself is
         * ignored, the implementation class knows, that some message arrived (it
         * called dispatch_message)
         */
        pong = 3,
        ///notifies, that new profile has been created on the server, and it is also send as result of succesful authentification
        /**
         *
         */
        welcome = 4,
        ///sent by one side to request authentication
        auth_req = 5,
        ///sent by other side to response authentication
        auth_response = 6,
        ///authentication failed - client should close connection
        auth_failed = 7
    };

    /* authentication schema
     *
     * (a) --- auth_req--------> (b)
     * (a) <-- auth_response---- (b)
     * (a) --- welcome---------> (b)
     *
     */



    AbstractBinaryBridge(Bus bus);

    ///called to output message to the stream
    virtual void output_message(std::string_view message) = 0;

    ///called when welcome message is parsed
    virtual void on_welcome() = 0;

    ///called when auth request message is parsed
    /**
     * @param proof_type type of proof (type of digest)
     * @param salt random string (at least 128bit entropy) used as salt or message to
     *    build as digest. For example if the request of digest is HMAC256, than salt
     *    is the message which is hashed by secret password. The result of hash
     *    is sent as proof.
     */
    virtual void on_auth_request(std::string_view proof_type, std::string_view salt) = 0;

    ///called when auth response message is parsed
    /**
     * @param ident identity
     * @param proof proof if identity
     * @param salt salt used in the transaction
     */
    virtual void on_auth_response(std::string_view ident, std::string_view proof, std::string_view salt) = 0;

    ///dispatch message from other side
    bool dispatch_message(std::string_view message);

    ///Request authentification from other side
    /**
     * @param digest_type specifies digest type. The class doesn't specify any digest types, so
     * it is defined as string. The other side must know type of digest and how to generate it.
     *
     * This function also disables bridge feature until welcome is send_welcome is called. If
     * auth response is parsed, then on_auth_response function is called
     */
    void request_auth(std::string_view digest_type);

    ///sends ping message
    void send_ping();

    ///sends pong message
    void send_pong();

    ///sends welcome message
    /**
     * It also enables bridge feature if has been disabled
     */
    void send_welcome();

    ///sends authentification response
    /**
     * @param ident identity identification
     * @param proof proof of identity (digest)
     */
    void send_auth_response(std::string_view ident, std::string_view proof);

    void send_auth_failed();

    ///returns true if bridge is disabled, because it waits for authentification
    bool disabled() const {return !_disabled;}

public:

    ///maximum size of message which can fit to a static buffer
    static constexpr std::size_t static_output_buffer_size = 1024;
    ///maximum count of channels which can fit to a static buffer
    static constexpr std::size_t static_channel_list_buffer = 32;

     ///Write unsinged int to buffer
     template<std::output_iterator<char> Iter>
     static Iter write_uint(Iter out, std::size_t val);
     ///Write string to buffer
     template<std::output_iterator<char> Iter>
     static Iter write_string(Iter out, const std::string_view &str);

     static std::size_t get_uint_size(char c);
     ///Read unsigned int
     static std::uint64_t read_uint(std::string_view &msgtext);
     ///Read string
     static std::string_view read_string(std::string_view &msgtext);
     ///Write whole message
     template<std::output_iterator<char> Iter>
     static Iter write_message(Iter out, const Message &msg);
     ///Read whole message
     template<typename Factory>
     static auto read_message(Factory &&factory, std::string_view msgtext)
     -> decltype(factory(std::string_view(), std::string_view(), std::string_view(), std::uint32_t(0)));

     ///Write channel list
     /**
      * @param buffer a character buffer
      * @param list list to write. You can specify multiple lists to combine these lists
      * into one
      */
     template<std::output_iterator<char> Iter, typename ... ChanList>
     static Iter write_channel_list(Iter out, const ChanList &...list);

     ///Read channel list
     /**
      * @param msgtext text of the message
      * @return list of channels - note, the content is taken from the message, do not
      * discard the message until the list is processed somehow
      */
     template<std::output_iterator<ChannelID> Iter>
     static Iter read_channel_list(std::string_view &msgtext, Iter iter);


     static std::size_t read_channel_list_count(std::string_view &msgtext);


protected:

     bool _disabled = false;
     char _salt[16];

    ///overide - send channels to other side
    virtual void send_channels(const ChannelList &channels) noexcept override;
    ///overide - send message to other side
    virtual void send_message(const Message &msg) noexcept override;

    void parse_channels(std::string_view message);
    void parse_message(std::string_view message);
    void parse_auth_req(std::string_view message);
    void parse_auth_resp(std::string_view message);
};


template<std::output_iterator<char> Iter>
inline Iter AbstractBinaryBridge::write_uint(Iter out, std::size_t val) {
    //writes uint in compressed form |xxxNNNNN|NNNNNNNN|NNNNNNNN|....
    //where 'xxx' is count of extra bytes, so number 128 is written as 00100000|10000000

    auto tmp = val;
    auto bytes = 0;
    while (tmp > 0x1F) {    //calculate extra bytes to fit
        ++bytes; tmp >>=8;
    }
    tmp |= (bytes << 5); //combine first byte with length
    *out=static_cast<char>(tmp);   //write it
    ++out;
    while (bytes) { //write remaining bytes
        --bytes;
        char c = static_cast<char>((val >> (bytes * 8)) & 0xFF);
        *out = c;
        ++out;
    }
    return out;
}

template<std::output_iterator<char> Iter>
inline Iter AbstractBinaryBridge::write_string(Iter out, const std::string_view &str) {
    out = write_uint(out, str.size());
    out = std::copy(str.begin(), str.end(), out);
    return out;
}


inline std::size_t AbstractBinaryBridge::get_uint_size(char c) {
    return static_cast<unsigned char>(c) >> 5;
}

inline std::size_t AbstractBinaryBridge::read_uint(std::string_view &msgtext) {
    //read uint in compressed form
    std::size_t ret;
    ret = static_cast<unsigned char>(msgtext.front());  //read first byte
    msgtext = msgtext.substr(1);    //go next
    auto bytes = ret >> 5;      //extract length
    ret = ret & 0x1F;           //remove length from first byte
    while (bytes && !msgtext.empty()) { //read and append next bytes
        ret = (ret << 8) | static_cast<unsigned char>(msgtext.front());
        msgtext = msgtext.substr(1);
        --bytes;
    }
    //result
    return ret;
}

inline std::string_view AbstractBinaryBridge::read_string(std::string_view &msgtext) {
    auto len = read_uint(msgtext);
    auto part = msgtext.substr(0,len);
    msgtext = msgtext.substr(part.size());
    return part;
}

template<std::output_iterator<char> Iter>
inline Iter AbstractBinaryBridge::write_message(Iter out, const Message &message) {
    *out = static_cast<char>(MessageType::message);
    ++out;
    auto channel = message.get_channel();
    auto content = message.get_content();
    auto convid = message.get_conversation();
    auto sender = message.get_sender();
    out = write_uint(out,convid);
    out = write_string(out, sender);
    out = write_string(out, channel);
    out = write_string(out, content);
    return out;
}

template<typename Factory>
inline auto AbstractBinaryBridge::read_message(Factory &&factory, std::string_view msgtext)
-> decltype(factory(std::string_view(), std::string_view(), std::string_view(), std::uint32_t(0))) {
    auto convid = read_uint(msgtext);
    auto sender = read_string(msgtext);
    auto channel = read_string(msgtext);
    auto content = read_string(msgtext);
    return  factory(sender, channel, content, convid);
}

template<std::output_iterator<char> Iter, typename ... ChanList>
inline Iter AbstractBinaryBridge::write_channel_list(Iter out, const ChanList & ... list) {
    *out = static_cast<char>(MessageType::channels);
    ++out;
    auto total_count  = (static_cast<std::size_t>(0) + ... + list.size());
    out = write_uint(out, total_count);
    if constexpr(sizeof...(list)>0) {
        auto write_list = [&](const auto &l) {
            for (const auto &chan: l) {
                out = write_string(out, chan);
            }
        };
        (write_list(list),...);
    }
    return out;
}



inline std::size_t AbstractBinaryBridge::read_channel_list_count(std::string_view &msgtext) {
    return read_uint(msgtext);;
}



template<std::output_iterator<ChannelID> Iter>
inline Iter AbstractBinaryBridge::read_channel_list(std::string_view &msgtext, Iter iter) {
    auto count = read_uint(msgtext);
    for (std::size_t i = 0; i < count; i++) {
        *iter = read_string(msgtext);
        ++iter;
    }
    return iter;
}

}


