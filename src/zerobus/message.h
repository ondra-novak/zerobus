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

///abstract message
class IMessage {
public:
    virtual ChannelID get_sender() const = 0;
    virtual ChannelID get_channel() const = 0;
    virtual MessageContent get_content() const = 0;
    virtual ConversationID get_conversation() const = 0;
    virtual ~IMessage() = default;
};

///message wrapper
class Message {
public:
    Message(const std::shared_ptr<const IMessage> &ptr):_ptr(ptr) {}
    ///Retrieve sender
    /**
     * @return retrieves sender's mailbox address. If you need to send a response to
     * the sender directly, you simply use this address as channel.
     *
     * Every listener subscribes to its local mailbox by sending a message for the first
     * time
     */
    ChannelID get_sender() const {return _ptr->get_sender();}
    ///Retrieve channel name
    /**
     * In case that message is private (see pm flag in function on_message), this contains
     * id of your private mailbox. Otherwise it contains channel name
     *  */
    ChannelID get_channel() const {return _ptr->get_channel();}

    /**Retrieve message content
     * @return message content
     */
    MessageContent get_content() const {return _ptr->get_content();}

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
    ConversationID get_conversation() const {return _ptr->get_conversation();}

    bool operator==(const Message &other) const = default;

protected:
    std::shared_ptr<const IMessage> _ptr;
};


}
