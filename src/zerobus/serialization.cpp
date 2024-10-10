#include "serialization.h"

namespace zerobus {

enum class MessageType: std::uint8_t {
        ///a message
        message = 0xFF,
        ///list of channels
        channels_replace = 0xFE,
        ///list of channels
        channels_add = 0xFD,
        ///list of channels
        channels_erase = 0xFC,
        ///send from other side that they unsubscribed all channels
        channels_reset = 0xFB,
        ///clear return path
        clear_path = 0xFA,
        ///add to group
        add_to_group = 0xF8,
        ///close group
        close_group = 0xF7,
};

Deserialization::Result Deserialization::operator ()(std::string_view msgtext, IBridgeAPI *api) {
    if (msgtext.empty()) return {};
    auto t = static_cast<MessageType>(msgtext[0]);
    msgtext = msgtext.substr(1);
    switch (t) {
        default: return UserMsg{static_cast<std::uint8_t>(t), msgtext};
        case MessageType::message: {
            auto cid = read_uint(msgtext);
            auto sender = read_string(msgtext);
            auto channel = read_string(msgtext);
            auto content = read_string(msgtext);
            return api->create_message(sender, channel, content, cid);
        }
        case MessageType::channels_replace:
        case MessageType::channels_add:
        case MessageType::channels_erase: {
            auto cnt = read_uint(msgtext);
            _channels.resize(cnt);
            for (std::size_t i = 0; i < cnt; ++i) {
                _channels[i] = read_string(msgtext);
            }
            constexpr Msg::Operation ops[] = {
                    Msg::Operation::erase,
                    Msg::Operation::add,
                    Msg::Operation::replace
            };
            return Msg::ChannelUpdate{Msg::ChannelList(_channels),
                ops[static_cast<int>(t) - static_cast<int>(MessageType::channels_erase)]};
        }
        case MessageType::clear_path: {
            auto sender = read_string(msgtext);
            auto receiver = read_string(msgtext);
            return Msg::ClearPath{sender,receiver};
        }
        case MessageType::add_to_group:{
            auto group = read_string(msgtext);
            auto target_id = read_string(msgtext);
            return Msg::AddToGroup{group, target_id};
        }
        case MessageType::close_group:{
            auto group = read_string(msgtext);
            return Msg::CloseGroup{group};
        }
        case MessageType::channels_reset: {
            return Msg::ChannelReset{};
        }

    }
}

std::back_insert_iterator<std::vector<char> > Serialization::start_write() {
    _buffer.clear();
    return std::back_inserter(_buffer);
}


std::string_view Serialization::operator ()(const Deserialization::UserMsg &msg) {
    auto iter = start_write();
    *iter = msg.type;
    ++iter;
    std::copy(msg.payload.begin(), msg.payload.end(), iter);
    return finish_write();
}

std::string_view Serialization::operator ()(const Message &msg) {
    compose_message(start_write(), MessageType::message,
            msg.get_conversation(),
            msg.get_sender(),
            msg.get_channel(),
            msg.get_content());
    return finish_write();

}

std::string_view Serialization::operator ()(const Msg::ChannelUpdate &msg) {
    constexpr MessageType types[] = {
            MessageType::channels_replace,
            MessageType::channels_add,
            MessageType::channels_erase,
    };
    auto iter = compose_message(start_write(), types[static_cast<int>(msg.op)], msg.lst.size());
    for (const auto &c: msg.lst) iter = write_string(iter, c);
    return finish_write();
}

std::string_view Serialization::operator ()(const Msg::ChannelReset &) {
    compose_message(start_write(), MessageType::channels_reset);
    return finish_write();
}

std::string_view Serialization::operator ()(const Msg::AddToGroup &msg) {
    compose_message(start_write(), MessageType::add_to_group,
            msg.group, msg.target);
    return finish_write();
}

std::string_view Serialization::operator ()(const Msg::ClearPath &msg) {
    compose_message(start_write(), MessageType::clear_path,
            msg.sender, msg.receiver);
    return finish_write();
}

std::string_view Serialization::operator ()(const Msg::CloseGroup &msg) {
    compose_message(start_write(), MessageType::close_group, msg.group);
    return finish_write();
}


std::uint64_t Deserialization::read_uint(std::string_view &msgtext) {
    if (msgtext.empty()) return 0;
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

std::string_view Deserialization::read_string(std::string_view &msgtext) {
    auto len = read_uint(msgtext);
    auto part = msgtext.substr(0,len);
    msgtext = msgtext.substr(part.size());
    return part;
}

std::string_view Serialization::finish_write() const {
    return {_buffer.data(), _buffer.size()};
}

}
