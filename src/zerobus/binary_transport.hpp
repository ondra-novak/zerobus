#pragma once
#include "bridge.hpp"
#include "utils/binary.hpp"

#include "utils/stack_alloc.hpp"

#include <iterator>
#include <cstring>
namespace zerobus {


template<typename Msg> struct Serialize;


template<typename Msg>
requires(std::is_trivially_copyable_v<Msg> && !std::is_base_of_v<bmsg::ChannelsBase, Msg>)
struct Serialize<Msg> {
    static std::size_t bin_size(const Msg &) {return sizeof(Msg);}
    static void to_binary(const Msg &msg, char *buffer) {
        std::memcpy(buffer, &msg, sizeof(msg));
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        auto sz = std::min<std::size_t>(sizeof(Msg), std::distance(from, to));
        std::memcpy(&msg, from, sz);
        return fn(msg);
    }
};
template<typename Msg>
requires(std::is_base_of_v<bmsg::ChannelsBase, Msg>) struct Serialize<Msg> {

    static std::size_t bin_size(const Msg &msg) {
        std::size_t res = bin::get_encoded_number_size(msg.lst.size());
        for(const auto &x: msg.lst) res+=bin::get_encoded_string_size(x);
        return res;
    }

    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_number(msg.lst.size(), iter);
        for (const auto &x: msg.lst) iter = bin::encode_string(x, iter);
    }

    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        std::size_t count = 0;
        from = bin::decode_number(count, from, to);
        return utils::stack_alloc<ChannelID>(count, [&](ChannelID *ptr){
           for (std::size_t i = 0; i < count; ++i) {
               from = bin::decode_string(ptr[i], from, to);
           }
           return fn(Msg{ChannelList(ptr, count)});
        });

    }

};

template<> struct Serialize<bmsg::ChannelReset>{
    static std::size_t bin_size(const bmsg::ChannelReset &) {return 0;}
    static void to_binary(const bmsg::ChannelReset &, char *) {}
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *, const char *) {
        fn(bmsg::ChannelReset{});
    }
};
template<> struct Serialize<bmsg::AddToGroup> {
    using Msg = bmsg::AddToGroup;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.group)
                +bin::get_encoded_string_size(msg.target);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);
        iter = bin::encode_string(msg.target, iter);
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        from = bin::decode_string(msg.group, from, to);
        from = bin::decode_string(msg.target, from, to);
        return fn(msg);
    }
};
template<> struct Serialize<bmsg::CloseGroup> {
    using Msg = bmsg::CloseGroup;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.group);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        from = bin::decode_string(msg.group, from, to);
        return fn(msg);
    }
};
template<> struct Serialize<bmsg::GroupEmpty> {
    using Msg = bmsg::GroupEmpty;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.group);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        from = bin::decode_string(msg.group, from, to);
        return fn(msg);
    }
};

template<> struct Serialize<Undelivered> {
    using Msg = Undelivered;

    static std::size_t bin_size(const Msg &msg) {
        return 2+bin::get_encoded_string_size(msg.sender)
                +bin::get_encoded_string_size(msg.target)
                +bin::get_encoded_number_size(msg.cid);
    }
    static void to_binary(const Msg &msg, char *iter) {
        *iter++ = static_cast<char>(msg.error);
        *iter++ = static_cast<char>(msg.importance);
        iter = bin::encode_string(msg.sender, iter);
        iter = bin::encode_string(msg.target, iter);
        iter = bin::encode_number(msg.cid, iter);
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        if (std::distance(from, to) < 3) return fn(msg);
        msg.error = static_cast<DeliveryError>(*from++);
        msg.importance = static_cast<Importance>(*from++);
        from = bin::decode_string(msg.sender, from, to);
        from = bin::decode_string(msg.target, from, to);
        from = bin::decode_number(msg.cid, from, to);
        return fn(msg);
    }
};
template<> struct Serialize<bmsg::Announce> {
    using Msg = bmsg::Announce;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.sender)
                +bin::get_encoded_number_size(msg.request_id);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.sender, iter);
        iter = bin::encode_number(msg.request_id, iter);
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        from = bin::decode_string(msg.sender, from, to);
        from = bin::decode_number(msg.request_id, from, to);
        return fn(msg);
    }
};

template<> struct Serialize<Message> {
    using Msg = Message;

    static std::size_t bin_size(const Msg &msg) {
        return 1+bin::get_encoded_string_size(msg.sender)
                +bin::get_encoded_string_size(msg.channel)
                +bin::get_encoded_string_size(msg.content)
                +bin::get_encoded_number_size(msg.cid);
    }
    static void to_binary(const Msg &msg, char *iter) {
        *iter++=static_cast<char>(msg.importance);
        iter = bin::encode_string(msg.sender, iter);
        iter = bin::encode_string(msg.channel, iter);
        iter = bin::encode_string(msg.content, iter);
        iter = bin::encode_number(msg.cid, iter);
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        msg.importance = static_cast<Importance>(*from++);
        from = bin::decode_string(msg.sender, from, to);
        from = bin::decode_string(msg.channel, from, to);
        from = bin::decode_string(msg.content, from, to);
        from = bin::decode_number(msg.cid, from, to);
        return fn(msg);
    }
};
template<> struct Serialize<bmsg::UpdateSerial> {
    using Msg = bmsg::UpdateSerial;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.serial);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.serial, iter);
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        from = bin::decode_string(msg.serial, from, to);
        return fn(msg);
    }
};
template<> struct Serialize<bmsg::NewSession> {
    using Msg = bmsg::NewSession;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_number_size(msg.version);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_number(msg.version, iter);
    }
    template<typename Fn>
    static auto from_binary(Fn &&fn, const char *from, const char *to) {
        Msg msg;
        from = bin::decode_number(msg.version, from, to);
        return fn(msg);
    }
};

template<typename T> static constexpr std::uint8_t message_id = 255;
template<> inline constexpr std::uint8_t message_id<bmsg::ChannelReset> = 0;
template<> inline constexpr std::uint8_t message_id<Message> = 1;
template<> inline constexpr std::uint8_t message_id<bmsg::AddChannels> = 2;
template<> inline constexpr std::uint8_t message_id<bmsg::EraseChannels> = 3;
template<> inline constexpr std::uint8_t message_id<bmsg::Announce> = 4;
template<> inline constexpr std::uint8_t message_id<Undelivered> = 5;
template<> inline constexpr std::uint8_t message_id<bmsg::AddToGroup> = 6;
template<> inline constexpr std::uint8_t message_id<bmsg::CloseGroup> = 7;
template<> inline constexpr std::uint8_t message_id<bmsg::GroupEmpty> = 8;
template<> inline constexpr std::uint8_t message_id<bmsg::UpdateSerial> = 9;
template<> inline constexpr std::uint8_t message_id<bmsg::NewSession> = 10;

using AllMessages = std::tuple<
        Message,
        bmsg::ChannelReset,
        bmsg::AddChannels,
        bmsg::EraseChannels,
        bmsg::Announce,
        Undelivered,
        bmsg::AddToGroup,
        bmsg::CloseGroup,
        bmsg::GroupEmpty,
        bmsg::UpdateSerial,
        bmsg::NewSession>;



template<typename Fn, unsigned int pos = 0>
static constexpr auto visit_by_id(std::uint8_t id, Fn &&fn) {
    if constexpr(pos >= std::tuple_size_v<AllMessages>) {
        return fn(std::type_identity<std::nullptr_t>{});
    } else if (id == message_id<std::tuple_element_t<pos, AllMessages> >) {
        return fn(std::type_identity<std::tuple_element_t<pos, AllMessages> >{});
    } else {
        return visit_by_id<Fn, pos+1>(id, std::forward<Fn>(fn));
    }
}

template<typename T>
concept OutputType = requires(T &&v, std::size_t sz) {
    {v.start(sz)} -> std::same_as<char *>;
    {v.commit(sz)} -> std::same_as<void>;
};

struct OutputTypeTest {
    char *start(std::size_t sz);
    void commit(std::size_t sz);
};

template<typename Ptr>
struct OutputTypeProxy {
    Ptr ptr;
    char *start(std::size_t sz) {
        return ptr->output_start(sz);
    }
    void commit(std::size_t sz) {
        return ptr->output_commit(sz);
    }
};


template<OutputType Output>
class BinaryTransport: public AbstractTransport {
public:



    BinaryTransport(Output output):_output(std::move(output)) {}

    ///Parses message and sends it to the bridge to be processed
    /**
     * @param message message in binary format
     * @retval 0 message was empty
     * @retval 1 message was processed
     * @retval >1 any other number contains type of message, if
     * message has unknown type. This notifies the caller that
     * message is probably their
     */
    std::uint8_t parse(std::string_view message) {
        std::uint8_t ret = 0;
        if (message.empty()) return ret;
        const char *from = message.data();
        const char *to = from + message.size();
        auto t = static_cast<std::uint8_t>(*from);
        ++from;
        ret = t;
        visit_by_id(t, [&](auto tag){
            using MsgType = typename decltype(tag)::type;
            if constexpr(!std::is_null_pointer_v<MsgType>)  {
                ret = 1;
                Serialize<MsgType>::from_binary([&](auto &&msg){
                    _target->receive(msg);
                }, from, to);
            }
        });
        return ret;
    }

    template<typename Msg>
    void send_message(std::uint8_t type, const Msg &msg) {
        std::size_t sz = Serialize<Msg>::bin_size(msg);
        ++sz;
        char *buffer = _output.start(sz);
        *buffer = type;++buffer;
        Serialize<Msg>::to_binary(msg, buffer);
        _output.commit(sz);
    }

protected:

    Output _output;
    IProtocol *_target ={};

    template<typename Msg>
    void send(const Msg &msg) {
        send_message(message_id<Msg>, msg);
    }

    virtual void set_target(IProtocol *target) override {_target = target;}
    virtual void receive(const Message &msg) noexcept override{send(msg);}
    virtual void receive(const Undelivered &msg) noexcept override{send(msg);}
    virtual void receive(const bmsg::EraseChannels &msg) noexcept override {send(msg);}
    virtual void receive(const bmsg::GroupEmpty &msg) noexcept override{send(msg);}
    virtual void receive(const bmsg::AddChannels &msg) noexcept override{send(msg);}
    virtual void receive(const bmsg::ChannelReset &msg) noexcept override{send(msg);}
    virtual void receive(const bmsg::NewSession &msg) noexcept override{send(msg);}
    virtual void receive(const bmsg::AddToGroup &msg) noexcept override{send(msg);}
    virtual void receive(const bmsg::CloseGroup &msg) noexcept override{send(msg);}
    virtual void receive(const bmsg::UpdateSerial &msg) noexcept override{send(msg);}
    virtual void receive(const bmsg::Announce &msg) noexcept override{send(msg);}


};


}

