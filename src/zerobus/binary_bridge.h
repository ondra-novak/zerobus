#include "bridge.h"
#include <functional>

namespace zerobus {

class BinaryBridge: public AbstractBridge {
public:

    enum class MessageType: char {
        message = 0,
        channels = 1
    };

    BinaryBridge(Bus bus, std::function<void(std::string_view)> output_fn);

    bool dispatch_message(std::string_view message);

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
     std::function<void(std::string_view)> _output_fn;

    ///overide - send channels to other side
    virtual void send_channels(const ChannelList &channels) noexcept override;
    ///overide - send message to other side
    virtual void send_message(const Message &msg) noexcept override;

    void parse_channels(std::string_view message);
    void parse_message(std::string_view message);
};


template<std::output_iterator<char> Iter>
inline Iter BinaryBridge::write_uint(Iter out, std::size_t val) {
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
inline Iter BinaryBridge::write_string(Iter out, const std::string_view &str) {
    out = write_uint(out, str.size());
    out = std::copy(str.begin(), str.end(), out);
    return out;
}


inline std::size_t BinaryBridge::read_uint(std::string_view &msgtext) {
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

inline std::string_view BinaryBridge::read_string(std::string_view &msgtext) {
    auto len = read_uint(msgtext);
    auto part = msgtext.substr(0,len);
    msgtext = msgtext.substr(part.size());
    return part;
}

template<std::output_iterator<char> Iter>
inline Iter BinaryBridge::write_message(Iter out, const Message &message) {
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
inline auto BinaryBridge::read_message(Factory &&factory, std::string_view msgtext)
-> decltype(factory(std::string_view(), std::string_view(), std::string_view(), std::uint32_t(0))) {
    auto convid = read_uint(msgtext);
    auto sender = read_string(msgtext);
    auto channel = read_string(msgtext);
    auto content = read_string(msgtext);
    return  factory(sender, channel, content, convid);
}

template<std::output_iterator<char> Iter, typename ... ChanList>
inline Iter BinaryBridge::write_channel_list(Iter out, const ChanList & ... list) {
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



inline std::size_t BinaryBridge::read_channel_list_count(std::string_view &msgtext) {
    return read_uint(msgtext);;
}



template<std::output_iterator<ChannelID> Iter>
inline Iter BinaryBridge::read_channel_list(std::string_view &msgtext, Iter iter) {
    auto count = read_uint(msgtext);
    for (std::size_t i = 0; i < count; i++) {
        *iter = read_string(msgtext);
        ++iter;
    }
    return iter;
}

}
