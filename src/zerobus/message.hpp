#pragma once
#include "types.hpp"
#include "importance.hpp"
#include "utils/serializer.hpp"
#include <memory>
#include <string_view>
#include <vector>

namespace zerobus {





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
    ///Specifies message importance
    /**
     * This enum controls how message is carried over bridges with
     * finite transfer speed and latency. It helps to controls the flow
     * of the trafic
     */
    Importance importance;

    ///Returns size in bytes of underlying data.
    /**
     * @return size in bytes of underlying data - need for copy();
     */
    std::size_t size_bytes() const {
        return sender.size()+channel.size()+content.size()+3;
    }

    ///Makes copy of the message isolating also its data
    /**
     * @param buffer pointer to character buffer. Must be preallocated
     * to correct size. Use size_bytes() to determine, how much
     * bytes are needed
     *
     * @return copied message
     */
    Message copy(char *buffer) const {
        Message ret;
        auto iter = buffer;
        auto copydata = [&](std::string_view data){
            auto end = std::copy(data.begin(), data.end(), iter);
            *end = '\0';
            auto ret  = std::string_view(iter, end);
            iter = end+1;
            return ret;
        };
        ret.sender = copydata(sender);
        ret.channel = copydata(channel);
        ret.content = copydata(content);
        ret.importance = importance;
        ret.cid = cid;
        return ret;
    }

    ///Makes copy of the message isolating also its data
    /**
     * @param data reference to an vector, which receives message's underlying
     * data
     * @return copied message
     *
     * @note because message itself acts as refrence, you also need
     *  to store the underlying data to keep copied message valid.
     */
    Message copy(std::vector<char> &data) const {
        data.resize(size_bytes());
        return copy(data.data());

    }

    ///Unpack binary message into type T
    template<typename T>
    T unpack() const {
        auto iter = content.data();
        auto end = iter+content.size();
        return bin::Serializer<T>::desrl(iter, end);
    }

    ChannelID get_sender() const {return sender;}
    ChannelID get_channel() const {return channel;}
    MessageContent get_content() const {return content;}
    ConversationID get_conversation() const {return cid;}


};

}
