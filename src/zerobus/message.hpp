#pragma once
#include <cstdint>
#include <memory>
#include <string_view>
#include <span>

namespace zerobus {


using ChannelID = std::string_view;
///messages are string
using MessageContent = std::string_view;
///conversation id - using number is enough
using ConversationID = std::uint32_t;

using ChannelList = std::span<const ChannelID>;
using ChannelListMutable = std::span<ChannelID>;

class Message {
public:

    Message(const ChannelID &sender, const ChannelID &channel,
            const MessageContent &content, ConversationID cid)
    :_data(MessageData::create_message(sender,channel,content,cid)) {}

    Message(const Message &other):Message(other.get_sender(),
            other.get_channel(), other.get_content(), other.get_conversation()) {}
    Message &operator=(const Message &other) {
        if (this != &other) {
            _data = MessageData::create_message(other.get_sender(),
            other.get_channel(), other.get_content(), other.get_conversation());
        }
        return *this;
    }

    Message(Message &&) = default;
    Message &operator=(Message &&) = default;


    ChannelID get_sender() const {
        return {_data->get_string_buffer(), _data->sender_size};
    }
    ChannelID get_channel() const {
        return {_data->get_string_buffer() + _data->sender_size, _data->channel_size};

    }
    MessageContent get_content() const {
        return {_data->get_string_buffer() + _data->sender_size + _data->channel_size, _data->content_size};
    }
    ConversationID get_conversation() const {
        return _data->conversation_id;
    }

protected:

    struct MessageAlloc {
        std::size_t extra_sz;
    };

    struct MessageData {
        std::size_t sender_size;
        std::size_t channel_size;
        std::size_t content_size;
        ConversationID conversation_id;
        char *get_string_buffer() {return reinterpret_cast<char *>(this+1);}
        const char *get_string_buffer() const {return reinterpret_cast<const char *>(this+1);}
        void *operator new(std::size_t sz, MessageAlloc info) {
            return ::operator new(sz+info.extra_sz);
        }
        void operator delete(void *ptr, MessageAlloc) {
            return ::operator delete(ptr);
        }
        void operator delete(void *ptr, std::size_t) {
            return ::operator delete(ptr);
        }

        static std::unique_ptr<MessageData> create_message(const ChannelID &sender,
                    const ChannelID &channel, const MessageContent &content,
                    ConversationID cid) {
            std::unique_ptr<MessageData> m (new(MessageAlloc{
                sender.size()+channel.size()+content.size()
            }) MessageData{
                sender.size(), channel.size(), content.size(), cid});
            std::copy(content.begin(), content.end(),
                    std::copy(channel.begin(), channel.end(),
                        std::copy(sender.begin(), sender.end(), m->get_string_buffer())));
            return m;
        }
    };

    std::unique_ptr<MessageData> _data;

};

}
