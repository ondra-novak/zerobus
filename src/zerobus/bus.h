#pragma once
#include "listener.h"
#include "functionref.h"
#include <span>

namespace zerobus {

class IBus {
public:

    using ChannelList = std::span<ChannelID>;

    virtual ~IBus() = default;

    virtual bool subscribe(IListener *listener, ChannelID channel) = 0;
    virtual void unsubscribe(IListener *listener, ChannelID channel) = 0;
    virtual void unsubscribe_all(IListener *listener) = 0;
    virtual void unsubcribe_private(IListener *listener) = 0;
    virtual bool add_to_group(ChannelID group_name, ChannelID uid) = 0;
    virtual void close_group(ChannelID group_name) = 0;
    virtual bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) = 0;
    virtual std::string get_random_channel_name(std::string_view prefix) const = 0;
    virtual bool is_channel(ChannelID id) const = 0;
    virtual void get_subscribed_channels(IListener *listener, FunctionRef<void(ChannelList) > &&cb) const = 0;

};

class Bus {
public:

    ///create new bus;
    static Bus create();


    Bus(std::shared_ptr<IBus> ptr):_ptr(ptr) {}
    ///subscribe channel
    /**
     * @param listener listener of messages
     * @param channel channel
     * @retval true subscribed
     * @retval false failed to subscribe (invalid channel name or private group)
     */
    bool subscribe(IListener *listener, ChannelID channel) {
        return _ptr->subscribe(listener, channel);
    }

    ///unsubscribe channel
    /**
     * @param listener listene to unsubscribe
     * @param channel
     */
    void unsubscribe(IListener *listener, ChannelID channel) {
        _ptr->unsubscribe(listener, channel);
    }
    ///unsubscribe listener from all channels
    /**
     * @param listener listener
     * after return, the associated object can be destroyed
     */
    void unsubscribe_all(IListener *listener) {
        _ptr->unsubscribe_all(listener);
    }

    ///unsubscribe private channel
    /**
     * This closes private channel and thus prevents to receive more private messages
     *
     * Private channel is created automatically when you specify a listener as
     * a parameter of the function send_message. This function deletes this
     * channel. Note that next usage of send_message creates new private channel
     * with a different address
     *
     * @param listener owner of a private channel.
     */
    void unsubcribe_private(IListener *listener) {
        _ptr->unsubcribe_private(listener);
    }

    ///Adds sender_id to private group
    /**
     * Private group is manually created channel. This channel cannot be subscribed, you
     * can add subscribes using this command. This is intended to create multicast groups
     * to which messages are sent. These groups are not distributed to neighboring nodes.
     *
     * @param group_name name of group
     * @param sender_id id of client to include. This can be client connected to different node.
     * There must be path to the client.
     * @retval true successful sent (invitation message has been forwarded to the target node, group is created)
     * @retval false failure. There can be already public channel with the same name, or
     * the path not found.
     *
     */
    bool add_to_group(ChannelID group_name, ChannelID sender_id) {
        return _ptr->add_to_group(group_name, sender_id);
    }

    ///Close group
    /**
     * unsubscribe all listeners on given group.
     * @param group_name name of group. You can also use this to close public channel
     */
    void close_group(ChannelID group_name) {
        _ptr->close_group(group_name);
    }

    ///send message
    /**
     * @param listener sender's listener. can be nullptr to send anonymous message
     * @param channel channel
     * @param msg message
     * @param cid conversation identifier, can be 0 if has no meaning
     * @retval true message has been posted (it doesn't indicate that has been delivered)
     * @retval false message was not posted (no information about how to route message)
     */
    bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid = 0) {
        return _ptr->send_message(listener, channel, msg, cid);
    }
    ///Generate random channel name
    /**
     * @param prefix channel name prefix
     * @return a random channel name with high entropy.
     *
     * @note useful to create ad-hoc multicast groups.
     */
    std::string get_random_channel_name(std::string_view prefix) const {
        return _ptr->get_random_channel_name(prefix);
    }

    bool is_channel(ChannelID id) const {
        return _ptr->is_channel(id);
    }

    ///retrieve subscribed channels for listener
    /**
     *
     * @param listener
     * @param cb callback function which receives a span of channels
     */
    template<std::invocable<std::span<ChannelID> > Callback>
    void get_subscribed_channels(IListener *listener, Callback &&cb) {
        _ptr->get_subscribed_channels(listener, cb);
    }


    auto get_handle() const {return _ptr;}



protected:
    std::shared_ptr<IBus> _ptr;

};

}
