#pragma once

#include "bridge.h"
#include <variant>

namespace zerobus {

class Deserialization {
public:

    struct UserMsg {
        std::uint8_t type = 0;
        std::string_view payload = {};
    };


    using Result = std::variant<
            UserMsg,
            Message,
            Msg::ChannelUpdate,
            Msg::ChannelReset,
            Msg::ClearPath,
            Msg::AddToGroup,
            Msg::CloseGroup>;

    Result operator()(std::string_view msgtext, IBridgeAPI *api);
public: //static helpers

    ///Read unsigned int
    static std::uint64_t read_uint(std::string_view &msgtext);
    ///Read string
    static std::string_view read_string(std::string_view &msgtext);


protected:
    std::vector<ChannelID> _channels;

};


class Serialization {
public:

    std::string_view operator()(const Deserialization::UserMsg &msg);
    std::string_view operator()(const Message &msg);
    std::string_view operator()(const Msg::ChannelUpdate &msg);
    std::string_view operator()(const Msg::ChannelReset &msg);
    std::string_view operator()(const Msg::AddToGroup &msg);
    std::string_view operator()(const Msg::ClearPath &msg);
    std::string_view operator()(const Msg::CloseGroup &msg);

    template<typename ... Args>
    std::string_view operator()(std::uint8_t msg_type, const Args & ... args) {
        auto iter = start_write();
        compose_message(iter, msg_type, args...);
        return finish_write();
    }

public: //static helpers

    template<std::output_iterator<char> Iter>
    static Iter write_uint(Iter out, std::size_t val);
    template<std::output_iterator<char> Iter>
    static Iter write_string(Iter out, const std::string_view &str);
    template<std::output_iterator<char> Iter, typename ... Args>
    static Iter compose_message(Iter out, const Args &... args);



protected:

    std::vector<char> _buffer;
    std::back_insert_iterator<std::vector<char> > start_write();
    std::string_view finish_write() const;
};




template<std::output_iterator<char> Iter>
inline Iter Serialization::write_uint(Iter out, std::size_t val) {
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
inline Iter Serialization::write_string(Iter out, const std::string_view &str) {
    out = write_uint(out, str.size());
    out = std::copy(str.begin(), str.end(), out);
    return out;
}
template<std::output_iterator<char> Iter, typename ... Args>
inline Iter Serialization::compose_message(Iter out, const Args &... args) {
    auto wr = [&](const auto &x){
        using T = std::decay_t<decltype(x)>;
        if constexpr(std::is_enum_v<T>) {
            *out = static_cast<char>(x);
            ++out;
        } else if constexpr(std::is_integral_v<T> && std::is_unsigned_v<T>) {
            out = write_uint(out, x);
        } else if constexpr(std::is_convertible_v<T, std::string_view>) {
            out = write_string(out, x);
        }
    };
    (...,wr(args));
    return out;
}


}
