#pragma once
#include "bridge.hpp"
#include "utils/binary.hpp"

#include "utils/stack_alloc.hpp"

#include <iterator>
#include <cstring>
namespace zerobus {


template<typename Msg> struct Serialize;

struct SerializeBase {
    template<typename Fn>
    static auto alloc_workspace(const char *, const char *, Fn &&fn) {
        return fn(nullptr);
    }
};

template<typename Msg>
requires(std::is_trivially_copyable_v<Msg> && !std::is_base_of_v<bmsg::ChannelsBase, Msg>)
struct Serialize<Msg>: SerializeBase {
    static std::size_t bin_size(const Msg &) {return sizeof(Msg);}
    static void to_binary(const Msg &msg, char *buffer) {
        std::memcpy(buffer, &msg, sizeof(msg));
    }
    static const char *from_binary(void *, Msg &msg, const char *from, const char *to) {
        auto sz = std::min<std::size_t>(sizeof(Msg), std::distance(from, to));
        std::memcpy(&msg, from, sz);
        return from + sz;
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
    static auto alloc_workspace(const char *from, const char *to, Fn &&fn) {
        std::size_t count = 0;
        bin::decode_number(count, from, to);
        return utils::stack_alloc<ChannelID>(count, std::forward<Fn>(fn));
    }

    static const char *from_binary(ChannelID *workspace, Msg &msg, const char *from, const char *to) {
        std::size_t count = 0;
        from = bin::decode_number(count, from, to);
        if (count) {
               for (std::size_t i = 0; i < count; ++i) {
                   from = bin::decode_string(workspace[i], from, to);
               }
        }
        msg.lst = ChannelList(workspace, count);
        return from;
    }
};

template<> struct Serialize<bmsg::ChannelReset>: SerializeBase {
    static std::size_t bin_size(const bmsg::ChannelReset &) {return 0;}
    static void to_binary(const bmsg::ChannelReset &, char *) {}
    static const char *from_binary(void *, bmsg::ChannelReset &,
            const char *from, const char *) {return from;}
};
template<> struct Serialize<bmsg::AddToGroup>: SerializeBase {
    using Msg = bmsg::AddToGroup;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.group)
                +bin::get_encoded_string_size(msg.target);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);
        iter = bin::encode_string(msg.target, iter);
    }
    static const char *from_binary(void *, Msg &msg, const char *from, const char *to) {
        from = bin::decode_string(msg.group, from, to);
        from = bin::decode_string(msg.target, from, to);
        return from;
    }
};
template<> struct Serialize<bmsg::CloseGroup>: SerializeBase {
    using Msg = bmsg::CloseGroup;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.group);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);
    }
    static const char *from_binary(void *, Msg &msg, const char *from, const char *to) {
        from = bin::decode_string(msg.group, from, to);
        return from;
    }
};
template<> struct Serialize<bmsg::GroupEmpty>: SerializeBase {
    using Msg = bmsg::GroupEmpty;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.group);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);
    }
    static const char *from_binary(void *, Msg &msg, const char *from, const char *to) {
        from = bin::decode_string(msg.group, from, to);
        return from;
    }
};

template<> struct Serialize<bmsg::NoRoute>: SerializeBase {
    using Msg = bmsg::NoRoute;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.sender)
                +bin::get_encoded_string_size(msg.receiver)
                +bin::get_encoded_number_size(msg.cid);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.sender, iter);
        iter = bin::encode_string(msg.receiver, iter);
        iter = bin::encode_number(msg.cid, iter);
    }
    static const char *from_binary(void *, Msg &msg, const char *from, const char *to) {
        from = bin::decode_string(msg.sender, from, to);
        from = bin::decode_string(msg.receiver, from, to);
        from = bin::decode_number(msg.cid, from, to);
        return from;
    }
};
template<> struct Serialize<Message>: SerializeBase {
    using Msg = Message;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.sender)
                +bin::get_encoded_string_size(msg.channel)
                +bin::get_encoded_string_size(msg.content)
                +bin::get_encoded_number_size(msg.cid);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.sender, iter);
        iter = bin::encode_string(msg.channel, iter);
        iter = bin::encode_string(msg.content, iter);
        iter = bin::encode_number(msg.cid, iter);
    }
    static const char *from_binary(void *, Msg &msg, const char *from, const char *to) {
        from = bin::decode_string(msg.sender, from, to);
        from = bin::decode_string(msg.channel, from, to);
        from = bin::decode_string(msg.content, from, to);
        from = bin::decode_number(msg.cid, from, to);
        return from;
    }
};
template<> struct Serialize<bmsg::UpdateSerial>: SerializeBase {
    using Msg = bmsg::UpdateSerial;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_string_size(msg.serial);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_string(msg.serial, iter);
    }
    static const char *from_binary(void *, Msg &msg, const char *from, const char *to) {
        from = bin::decode_string(msg.serial, from, to);
        return from;
    }
};
template<> struct Serialize<bmsg::NewSession>: SerializeBase {
    using Msg = bmsg::NewSession;

    static std::size_t bin_size(const Msg &msg) {
        return bin::get_encoded_number_size(msg.version);
    }
    static void to_binary(const Msg &msg, char *iter) {
        iter = bin::encode_number(msg.version, iter);
    }
    static const char *from_binary(void *, Msg &msg, const char *from, const char *to) {
        from = bin::decode_number(msg.version, from, to);
        return from;
    }
};

template<typename T> static constexpr std::uint8_t message_id = 255;
template<> constexpr std::uint8_t message_id<bmsg::ChannelReset> = 0;
template<> constexpr std::uint8_t message_id<Message> = 1;
template<> constexpr std::uint8_t message_id<bmsg::AddChannels> = 2;
template<> constexpr std::uint8_t message_id<bmsg::EraseChannels> = 3;
template<> constexpr std::uint8_t message_id<bmsg::SetChannels> = 4;
template<> constexpr std::uint8_t message_id<bmsg::NoRoute> = 5;
template<> constexpr std::uint8_t message_id<bmsg::AddToGroup> = 6;
template<> constexpr std::uint8_t message_id<bmsg::CloseGroup> = 7;
template<> constexpr std::uint8_t message_id<bmsg::GroupEmpty> = 8;
template<> constexpr std::uint8_t message_id<bmsg::UpdateSerial> = 9;
template<> constexpr std::uint8_t message_id<bmsg::NewSession> = 10;

using AllMessages = std::tuple<
        Message,
        bmsg::ChannelReset,
        bmsg::AddChannels,
        bmsg::EraseChannels,
        bmsg::SetChannels,
        bmsg::NoRoute,
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
                Serialize<MsgType>::alloc_workspace(from, to, [&](auto ptr){
                    MsgType msg;
                    Serialize<MsgType>::from_binary(ptr, msg, from, to);
                    _target->on_message(msg);
                });
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
    virtual void on_message(const Message &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::EraseChannels &msg) noexcept override {send(msg);}
    virtual void on_message(const bmsg::GroupEmpty &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::AddChannels &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::SetChannels &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::ChannelReset &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::NewSession &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::AddToGroup &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::CloseGroup &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::NoRoute &msg) noexcept override{send(msg);}
    virtual void on_message(const bmsg::UpdateSerial &msg) noexcept override{send(msg);}


};


}

