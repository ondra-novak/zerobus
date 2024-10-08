#include "bridge.h"
#include <functional>

namespace zerobus {

class AbstractBinaryBridge: public AbstractBridge {
public:

    enum class MessageType: char {
        ///a message
        message = 0,
        ///list of channels
        channels_replace = 1,
        ///list of channels
        channels_add = 2,
        ///list of channels
        channels_erase = 3,
        ///send from other side that they unsubscribed all channels
        channels_reset = 4,
        ///clear return path
        clear_path = 5,
        ///add to group
        add_to_group = 6,
        ///close group
        close_group = 7,



        ///notifies, successful login
        /**
         * it also assumes channels_reset (in case of re-login)
         */
        welcome = 8,
        ///sent by one side to request authentication
        auth_req = 9,
        ///sent by other side to response authentication
        auth_response = 10,
        ///authentication failed - client should close connection
        auth_failed = 11,

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
    bool disabled() const {return _disabled;}



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

     template<typename Fn>
     void output_message_helper(std::size_t req_size, Fn &&fn) {
         if (req_size <= static_output_buffer_size) {
             char buffer[static_output_buffer_size];
             auto iter = fn(static_cast<char *>(buffer));
             output_message(std::string_view(buffer, std::distance(buffer,iter)));
         } else {
             std::vector<char> buffer;
             buffer.resize(req_size);
             auto iter = fn(buffer.begin());
             output_message(std::string_view(buffer.data(), std::distance(buffer.begin(), iter)));
         }
     }

     template<typename ... Args>
     static std::size_t calc_required_space(const Args &... args) {
         auto clc = [](const auto &x) {
             if constexpr(std::is_enum_v<std::decay_t<decltype(x)> >) {
                 return 1;
             }else if constexpr(std::is_convertible_v<decltype(x), std::size_t>) {
                 char buff[10];
                 char *c = write_uint(buff, x);
                 return c - buff;
             } else {
                 static_assert(std::is_convertible_v<decltype(x), std::string_view>);
                 std::string_view v(x);
                 return calc_required_space(v.size())+v.size();
             }
         };
         return (0 + ... + clc(args));
     }

     template<typename ... Args>
     void output_message_fixed_len(const Args &... args) {
         std::size_t sz = calc_required_space(args...);
         auto build = [](auto &iter, const auto &x) {
             if constexpr(std::is_enum_v<std::decay_t<decltype(x)> >) {
                 *iter = static_cast<char>(x);
                 ++iter;
             }else if constexpr(std::is_convertible_v<decltype(x), std::size_t>) {
                 iter =  write_uint(iter,x);
             } else {
                 iter = write_string(iter, x);
             }
         };

         output_message_helper(sz, [&](auto iter){
             (...,build(iter, args));
             return iter;
         });
     }


protected:

     bool _disabled = false;
     char _salt[16];

    virtual void send_reset() noexcept override;
    virtual void send_channels(const ChannelList &channels, Operation op) noexcept override;
    virtual void send_message(const Message &msg) noexcept override;
    virtual void on_close_group(ChannelID group_name) noexcept override ;
    virtual void on_clear_path(ChannelID sender, ChannelID receiver) noexcept override;
    virtual void on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept override;



    void parse_channels(std::string_view message, Operation op);
    void parse_message(std::string_view message);
    void parse_auth_req(std::string_view message);
    void parse_auth_resp(std::string_view message);
    void parse_clear_path(std::string_view message);
    void parse_add_to_group(std::string_view message);
    void parse_close_group(std::string_view message);
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

}


