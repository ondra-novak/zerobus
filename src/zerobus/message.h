#pragma once

#include <memory>
#include <cstdint>
#include <string_view>

namespace zerobus {

using ChannelID = std::string_view;
///messages are string
using MessageContent = std::string_view;
///conversation id - using number is enough
using ConversationID = std::uint32_t;


///message wrapper
class Message {
public:

    Message(ChannelID sender, ChannelID channel, MessageContent content, ConversationID cid)
        :_sender(sender),_channel(channel),_content(content),_cid(cid) {}

    ///Retrieve sender
    /**
     * @return retrieves sender's mailbox address. If you need to send a response to
     * the sender directly, you simply use this address as channel.
     *
     * Every listener subscribes to its local mailbox by sending a message for the first
     * time
     */
    ChannelID get_sender() const {return _sender;}
    ///Retrieve channel name
    /**
     * In case that message is private (see pm flag in function on_message), this contains
     * id of your private mailbox. Otherwise it contains channel name
     *  */
    ChannelID get_channel() const {return _channel;}

    /**Retrieve message content
     * @return message content
     */
    MessageContent get_content() const {return _content;}

    ///Retrieve conversation ID
    /**
     * Conversation ID is an arbitrary number which can help to manage multiple
     * conversations especially in private messages. This number is
     * often sent with message to help identify conversation where this message
     * belongs. If used in channels, it is often used to provide a new
     * conversation ID for following private conversation.
     *
     * However, in general, this is just a number carried with the message.
     * Default value is zero.
     *
     * @return conversation ID of the message
     */
    ConversationID get_conversation() const {return _cid;}

    Message() = default;

    ~Message() {
        delete [] _buffer;
    }

    Message(const Message &other):_cid(other._cid) {
        _buffer = persist(other);
    }

    Message(Message &&other)
        :_sender(other._sender)
        ,_channel(other._channel)
        ,_content(other._content)
        ,_cid(other._cid)
        ,_buffer(other._buffer)
    {
        if (!_buffer) {
            _buffer = persist(other);
        }
        other._buffer = nullptr;
    }

    Message &operator=(const Message &other) {
        if (this != &other) {
            if (_buffer && calc_size() >= other.calc_size()) {
                persist(other, _buffer);
            } else {
                delete[] _buffer;
                _buffer = persist(other);
            }
        }
        return *this;
    }

    Message &operator=(Message &&other) {
        if (this != &other) {
            if (!other._buffer) {
                return operator=(other); //force persist
            }
            delete [] _buffer;
            _buffer = other._buffer;
            other._buffer = nullptr;
            _sender = other._sender;
            _channel = other._channel;
            _content = other._content;
            _cid = other._cid;
        }
        return *this;
    }




protected:
    ChannelID _sender;
    ChannelID _channel;
    MessageContent _content;
    ConversationID _cid;
    char *_buffer = nullptr;

    std::size_t calc_size() const {
        return _sender.size()+_channel.size()+_content.size()
                +bool(!_sender.empty())
                +bool(!_channel.empty())
                +bool(!_content.empty());
    }

    void persist(const Message &msg, char *iter) {
        _sender = {iter, msg._sender.size()};
        if (!_sender.empty()) {
            iter = std::copy(msg._sender.begin(), msg._sender.end(), iter);
            *iter++ = 0;
        }
        _channel = {iter, msg._channel.size()};
        if (!_channel.empty()) {
            iter = std::copy(msg._channel.begin(), msg._channel.end(), iter);
            *iter++ = 0;
        }
        _content = {iter, msg._content.size()};
        if (!_content.empty()) {
            iter = std::copy(msg._content.begin(), msg._content.end(), iter);
            *iter++ = 0;
        }
    }

    char *persist(const Message &msg) {
        char *buff = new char[msg.calc_size()];
        persist(msg, buff);
        return buff;
    }
};


}
