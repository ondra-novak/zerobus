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

    ///Contains address of the sender
    ChannelID sender;
    
    ///Contains channel name, group name, or private channel name if peer-to-peer
    ChannelID channel;
    
    ///Contains content of the message
    MessageContent content;
    
    /**The ConversationID represents an arbitrary number transmitted with the message,
     *  which can be used to distinguish between different conversations within the same connection.
     *  No specific values are predefined, and both parties must agree on their own numbering scheme.
     *  A recommended use case is in a request-response pattern, where the conversation ID of a
     *  request is copied to the response, allowing individual requests and responses to be matched.
     */
    ConversationID cid;


    ChannelID get_sender() const {return sender;}
    ChannelID get_channel() const {return channel;}
    MessageContent get_content() const {return content;}
    ConversationID get_conversation() const {return cid;}


};

}
