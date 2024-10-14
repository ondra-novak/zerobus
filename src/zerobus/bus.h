#pragma once
#include "listener.h"

#include <memory>
#include <span>
#include <vector>

namespace zerobus {

class IBus {
public:

    using ChannelList = std::span<ChannelID>;

    ///An object used to temporarily store the channel list obtained from the Bus object
    /**Because the Bus environment is dynamic and can change in the background,
     * it is necessary to ensure that the channel list obtained is firm and valid
     * at the time it is obtained. In addition, the object allows the reuse of
     * already allocated space from the previous use.
     *
     * The object contains locks that are held for the lifetime of the object.
     * Holding locks can cause some allocated memory that is no longer
     * needed to remain allocated. Therefore, it is a good idea to call
     * clear() when the list is no longer needed.
     */
    class ChannelListStorage  {
    public:

        ///construct storage
        ChannelListStorage() = default;
        ///retrieve stored channel list
        ChannelList get_channels() {return _channels;}
        ///clear all channels and release some memory
        /**
         * This doesn't affect a memory preallocated for next usage, it only releases
         * associated data with current list
         */
        void clear() {_channels.clear(); _locks.clear();}


        std::vector<ChannelID> _channels;
        std::vector<std::shared_ptr<std::nullptr_t> > _locks;
    };


    virtual ~IBus() = default;

    virtual bool subscribe(IListener *listener, ChannelID channel) = 0;
    virtual void unsubscribe(IListener *listener, ChannelID channel) = 0;
    virtual void unsubscribe_all(IListener *listener) = 0;
    virtual void unsubcribe_private(IListener *listener) = 0;
    virtual bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) = 0;
    virtual void close_group(IListener *owner, ChannelID group_name) = 0;
    virtual void close_all_groups(IListener *owner) = 0;
    virtual bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) = 0;
    virtual std::string get_random_channel_name(std::string_view prefix) const = 0;
    virtual bool is_channel(ChannelID id) const = 0;
    virtual ChannelList get_subscribed_channels(IListener *listener, ChannelListStorage &storage) const = 0;

};

class Bus {
public:


    using ChannelList = IBus::ChannelList;
    using ChannelListStorage = IBus::ChannelListStorage;
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
     * @param owner owner of the group. The group is automatically closed when owner calls unsubscribe_all.
     * The owner of the group can only post to the group. This argument can be nullptr, which
     * causes creating of public channel and adding the target client to it.
     *
     * @param group_name name of group
     * @param target_id Id of target client. There must exist path to this id
     * @retval true successful sent (invitation message has been forwarded to the target node, group is created)
     * @retval false failure. The group is probably exists and belongs to different owner. Or there
     * is no routing information to the target
     *
     *
     */
    bool add_to_group(IListener *owner, ChannelID group_name, ChannelID target_id) {
        return _ptr->add_to_group(owner, group_name, target_id);
    }

    ///Close group
    /**
     * unsubscribe all listeners on given group.
     * @param owner owner of the group. You can "force close" public channel by specifying nullptr here
     * @param group_name name of group. You can also use this to close public channel
     */
    void close_group(IListener *owner, ChannelID group_name) {
        _ptr->close_group(owner, group_name);
    }

    ///close all groups owned by given owner
    /**
     * @param owner owner
     */
    virtual void close_all_groups(IListener *owner) {
        _ptr->close_all_groups(owner);
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

    ///Tests, whether channel exists
    /**
     * @param id channel name
     * @retval true channel exists
     * @retval false channel not found
     *
     * @note this includes groups as well. You cannot check for mailbox id
     */
    bool is_channel(ChannelID id) const {
        return _ptr->is_channel(id);
    }

    ///Retrieve subscribed channel by this listener
    /**
     * @param listener listener
     * @param storage object used as storage for channel data and makes return value valid. You need
     * to keep this object while you processing the result
     * @return list of channels. List is always ordered (std::less<std::string>)
     */
     ChannelList get_subscribed_channels(IListener *listener, ChannelListStorage &storage) const {
         return _ptr->get_subscribed_channels(listener, storage);
     }


    auto get_handle() const {return _ptr;}



protected:
    std::shared_ptr<IBus> _ptr;

};

}
