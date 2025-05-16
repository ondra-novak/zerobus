#pragma once
#include "bridge.hpp"
#include "utils/binary.hpp"

#include "utils/stack_alloc.hpp"

namespace zerobus {



template<typename Output>
class BinaryTransport: public AbstractTransport {
public:

    template<typename T> static constexpr std::uint8_t message_id = 255;
    template<> static constexpr std::uint8_t message_id<MsgChannelReset> = 0;
    template<> static constexpr std::uint8_t message_id<Message> = 1;
    template<> static constexpr std::uint8_t message_id<MsgAddChannels> = 2;
    template<> static constexpr std::uint8_t message_id<MsgEraseChannels> = 3;
    template<> static constexpr std::uint8_t message_id<MsgSetChannels> = 4;
    template<> static constexpr std::uint8_t message_id<MsgNoRoute> = 5;
    template<> static constexpr std::uint8_t message_id<MsgAddToGroup> = 6;
    template<> static constexpr std::uint8_t message_id<MsgCloseGroup> = 7;
    template<> static constexpr std::uint8_t message_id<MsgGroupEmpty> = 8;
    template<> static constexpr std::uint8_t message_id<MsgUpdateSerial> = 9;
    template<> static constexpr std::uint8_t message_id<MsgNewSession> = 10;

    using AllMessages = std::tuple<MsgChannelReset,
            Message,
            MsgAddChannels,
            MsgEraseChannels,
            MsgSetChannels,
            MsgNoRoute,
            MsgAddToGroup,
            MsgCloseGroup,
            MsgGroupEmpty,
            MsgUpdateSerial,
            MsgNewSession>;


    BinaryTransport(Output output):_output(std::move(output)) {}

    void parse(std::string_view message) {
        if (message.empty()) return;
        const char *from = message.data();
        const char *to = from + message.size();
        auto t = static_cast<std::uint8_t>(*from);
        ++from;
        visit_by_id(t, [&](auto tag){
           parse_msg<typename decltype(tag)::type>(from, to);
        });
    }

protected:

    Output _output;
    IProtocol *_target = nullptr;

    template<typename T> struct TypeTag {using type = T;};

    template<typename Fn, unsigned int pos = 0>
    static constexpr auto visit_by_id(std::uint8_t id, Fn &&fn) {
        if constexpr(pos >= std::tuple_size_v<AllMessages>) {
            return fn(TypeTag<std::nullptr_t>{});
        } else if (id == message_id<std::tuple_element_t<pos, AllMessages> >) {
            return fn(TypeTag<std::tuple_element_t<pos, AllMessages> >{});
        } else {
            return visit_by_id<Fn, pos+1>(id, std::forward<Fn>(fn));
        }
    }


    static std::size_t get_msg_size(const MsgChannelsBase &msg) {
        std::size_t res = bin::get_encoded_number_size(msg.lst.size());
        for(const auto &x: msg.lst) res+=bin::get_encoded_string_size(x);
        return res;
    }
    static std::size_t get_msg_size(const Message &msg) {
        return bin::get_encoded_string_size(msg.sender)
                + bin::get_encoded_string_size(msg.channel)
                + bin::get_encoded_string_size(msg.content)
                + bin::get_encoded_number_size(msg.cid);
    }

    static std::size_t get_msg_size(const MsgUpdateSerial &msg) {
        return bin::get_encoded_string_size(msg.serial);
    }
    static std::size_t get_msg_size(const MsgChannelReset &) {
        return 0;
    }
    static std::size_t get_msg_size(const MsgNewSession &msg) {
        return bin::get_encoded_number_size(msg.version);
    }
    static std::size_t get_msg_size(const MsgNoRoute &msg) {
        return  bin::get_encoded_string_size(msg.sender)
                + bin::get_encoded_string_size(msg.receiver)
                + bin::get_encoded_number_size(msg.cid);
    }
    static std::size_t get_msg_size(const MsgCloseGroup &msg) {
        return bin::get_encoded_string_size(msg.group);
    }
    static std::size_t get_msg_size(const MsgGroupEmpty &msg) {
        return bin::get_encoded_string_size(msg.group);
    }
    static std::size_t get_msg_size(const MsgAddToGroup &msg) {
        return bin::get_encoded_string_size(msg.group);
              +bin::get_encoded_string_size(msg.target);
    }

    static void serialize_msg(const MsgChannelsBase &msg, char *iter) {
        iter = bin::encode_number(msg.lst.size(), iter);
        for (const auto &x: msg.lst) iter = bin::encode_string(x, iter);

    }
    static void serialize_msg(const MsgGroupEmpty &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);
    }
    static void serialize_msg(const MsgChannelReset &, char *) {
    }
    static void serialize_msg(const MsgNewSession &msg, char *iter) {
        iter = bin::encode_number(msg.version, iter);
    }
    static void serialize_msg(const MsgAddToGroup &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);
        iter = bin::encode_string(msg.target, iter);
    }
    static void serialize_msg(const Message &msg, char *iter) {
        iter = bin::encode_number(msg.cid, iter);
        iter = bin::encode_string(msg.sender, iter);
        iter = bin::encode_string(msg.channel, iter);
        iter = bin::encode_string(msg.content, iter);
    }
    static void serialize_msg(const MsgCloseGroup &msg, char *iter) {
        iter = bin::encode_string(msg.group, iter);

    }
    static void serialize_msg(const MsgNoRoute &msg, char *iter) {
        iter = bin::encode_number(msg.cid, iter);
        iter = bin::encode_string(msg.sender,iter);
        iter = bin::encode_string(msg.receiver, iter);
    }
    static void serialize_msg(const MsgUpdateSerial &msg, char *iter) {
        iter = bin::encode_string(msg.serial, iter);
    }

    template<typename Msg>
    void send_msg(const Msg &msg) {
        std::size_t sz = get_msg_size(msg);
        utils::stack_alloc<char>(sz, [&](char *buffer){
            *buffer++ = message_id<Msg>;
            serialize_msg(msg, buffer);
            _output(std::string_view(buffer,sz));
        });
    }

    template<typename Msg>
    void parse_msg(const char *from, const char *to) {
        if constexpr(std::is_base_of_v<MsgChannelsBase, Msg>) {
            std::size_t count = 0;
            bin::decode_number(count, from, to);
            if (count) {
                utils::stack_alloc<ChannelID>(count, [&](ChannelID *lst){
                   for (std::size_t i = 0; i < count; ++i) {
                       bin::decode_string(lst[i], from, to);
                   }
                   Msg m{ChannelList(lst, count)};
                   _target->on_message(m);
                });
            }
        } else {
            Msg msg;
            if constexpr(std::is_same_v<Msg, MsgAddToGroup>) {
                bin::decode_string(msg.group, from, to);
                bin::decode_string(msg.target, from, to);
            } else if constexpr(std::is_same_v<Msg, MsgGroupEmpty>) {
                bin::decode_string(msg.group, from, to);
            } else if constexpr(std::is_same_v<Msg, MsgCloseGroup>) {
                bin::decode_string(msg.group, from, to);
            } else if constexpr(std::is_same_v<Msg, MsgUpdateSerial>) {
                bin::decode_string(msg.serial, from, to);
            } else if constexpr(std::is_same_v<Msg, MsgNoRoute>) {
                bin::decode_number(msg.cid, from, to);
                bin::decode_string(msg.sender, from, to);
                bin::decode_string(msg.receiver, from, to);
            } else if constexpr(std::is_same_v<Msg, Message>) {
                bin::decode_number(msg.cid, from, to);
                bin::decode_string(msg.sender, from, to);
                bin::decode_string(msg.channel, from, to);
                bin::decode_string(msg.content, from, to);
            } else if constexpr(std::is_same_v<Msg, MsgNewSession>) {
                bin::decode_number(msg.version, from, to);
            } else if constexpr(std::is_same_v<Msg, MsgChannelReset>) {
            } else {
                static_assert(std::is_same_v<Msg, std::nullptr_t>);
                return;
            }
            if constexpr(!std::is_same_v<Msg, std::nullptr_t>) {
                _target->on_message(msg);
            }
        }

    }

    virtual void set_target(IProtocol *target) override {_target = target;}
    virtual void on_message(const MsgEraseChannels &msg) noexcept override {send_msg(msg);}
    virtual void on_message(const MsgGroupEmpty &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const MsgAddChannels &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const MsgSetChannels &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const MsgChannelReset &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const MsgNewSession &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const MsgAddToGroup &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const Message &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const MsgCloseGroup &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const MsgNoRoute &msg) noexcept override{send_msg(msg);}
    virtual void on_message(const MsgUpdateSerial &msg) noexcept override{send_msg(msg);}


};


}

