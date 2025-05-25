#pragma once

#include "bus.hpp"
#include "listener.hpp"
#include "utils/overloaded.hpp"

#include "channel_list_storage.hpp"

namespace zerobus {


class Client: public IListener {
public:

    Client(Bus bus): _bus(std::move(bus)) {}

    ~Client() {unsubscribe_all();}

    /// Subscribes a listener to a specific channel.
    /**
     * This function registers the provided listener to receive notifications
     * from the specified channel.
     *
     * @param channel The ID of the channel to subscribe to.
     * @retval true if the subscription was successful.
     * @retval false if the channel name is invalid or reserved.
     */
    bool subscribe(ChannelID channel) {
        return _bus.subscribe(this, channel);
    }

    /// Subscribes a listener to multiple channels.
    /**
     * This method allows registering a listener to a list of channels in bulk.
     * It returns `true` if all channels were successfully subscribed, and `false`
     * if one or more channels could not be subscribed. In case of failure, the caller
     * should use the `get_subscribed_channels` method to determine which channels
     * were successfully subscribed.
     *
     * @param channel List of channels to subscribe the listener to.
     * @retval true if all channels were successfully subscribed
     * @retval false otherwise.
     */
    bool subscribe( ChannelList channels) {
        return _bus.subscribe(this, channels);
    }

    /// Unsubscribes a listener from a specific channel.
    /**
     * This method removes the provided listener from the specified channel.
     * If the listener was not subscribed to the channel, the method does nothing.
     * The operation is guaranteed not to fail.
     *
     * @param channel The ID of the channel to unsubscribe from.
     */
    void unsubscribe(ChannelID channel) {
        _bus.unsubscribe(this, channel);
    }

    /// Unsubscribe multiple channels
    /**
     * @param channels list of channels
     */
    void unsubscribe(ChannelList channels) {
        _bus.unsubscribe(this, channels);
    }

    /// Unsubscribes the listener from everything
    /**
     * Unsubscribes the listener from all channels and ensures that there are no
     * remaining references to the listener within the bus system.
     *
     * This function must be called before the listener is destroyed to prevent
     * undefined behavior. It guarantees that all pending messages for the listener
     * are delivered before the listener is fully unsubscribed.
     *
     *
     * @note This function blocks until all pending messages are processed and
     * delivered. During this period, the listener may still receive messages
     * sent from other threads.
     */
    void unsubscribe_all() {_bus.unsubscribe_all(this);}

    ///Close private channel
    /**
     * Disassociates the listener from its private channel and closes the channel.
     *
     * When a listener instance is used as a sender's reference, a private channel
     * is automatically created. This private channel facilitates receiving direct
     * messages, such as replies from entities listening on other channels.
     *
     * Calling this function ensures that the listener's private channel is properly
     * closed and any associated resources are released. After this operation, the
     * listener will no longer have a private channel for direct communication.
     *
     * @note this function doesn't remove the listener from groups even if its
     * channel name was used as member identification
     *
     * @note any message routed to this private channel after the channel is closed
     * may be returned to the sender through on_delivery_error().
     */
    void close_private_channel() {_bus.close_private_channel(this);}


    /// Adds a listener to a group.
    /**
     * This method allows adding a "remote" listener (member) to a group owned by a
     * "local" listener (owner). If the group does not exist, it is created, and the
     * specified local listener becomes the owner of the group.
     *
     * A group functions as a channel but only supports one-way communication. The
     * local listener (owner) can broadcast messages to all its members, while
     * remote listeners (members) can respond using direct messages to the owner.
     *
     * Only the owner has the authority to add new members to the group. However,
     * any member can remove themselves from the group by calling either
     * `unsubscribe_channel()` or `unsubscribe_all()`.
     *
     * @param group_name The name of the group to which the remote listener is
     *                   being added.
     * @param uid The name of the private channel for the newly added remote
     *            listener (member).
     * @retval true The remote listener was successfully added to the group.
     * @retval false The remote listener could not be added. This may occur if the
     *               specified local listener does not own the group, the group
     *               name is reserved, or the group name is invalid.
     */
    bool add_to_group(ChannelID group_name, ChannelID uid, ConversationID cid) {
        return _bus.add_to_group(this, group_name, uid, cid);
    }

    ///Closes the group, removes all members
    /**
     * @param group_name name of group to close
     */
    void close_group(ChannelID group_name) {
        _bus.close_group(this, group_name);
    }

    ///Close all groups associated with the listener's pointer
    /**
     *
     * @note you don't need to call this function if unsubscribe_all() is
     * eventually used
     */
    void close_all_groups() {
        _bus.close_all_groups(this);
    }

    ///Send message to a channel
    /**
     * Sends a message to a specified channel.
     *
     *
     * @param channel  The name of the target channel. This can be a public channel, a private
     *                 channel, or a group. If a group is specified, the listener must also be
     *                 the owner of the group.
     *
     * @param msg      The content of the message (payload).
     *
     * @param cid      The conversation ID. This allows identification of a specific conversation
     *                 in cases where multiple conversations are active on the same channel. The
     *                 number is carried along with the message and can also be used as an arbitrary
     *                 identifier for further tracking.
     *
     * @param importance Specifies message importance, see Importance for list of options
     *
     * @retval true    The message was successfully sent. Note that this does not guarantee delivery.
     *
     * @retval false   The message could not be sent due to various reasons, such as the target
     *                 channel not existing, missing routing information for the target channel,
     *                 or the channel being a group where the listener is not the owner.
     *
     * @note If the specified channel is a private channel that has already been closed, the function
     *       may still return true. However, the listener may asynchronously receive an error through
     *       the `on_delivery_error()` callback.
     */
    bool send_message( ChannelID channel, MessageContent msg, ConversationID cid = 0, Importance importance = Importance::normal) {
        return _bus.send_message(this, channel, msg, cid, importance);
    }

    /// Retrieves the list of channels subscribed by a specific listener.
    /**
     * This function returns a list of channels to which the specified listener is currently subscribed.
     *
     * @return A list of channel IDs representing the channels to which the listener is subscribed.
     *         The list will be empty if the listener has no active subscriptions.
    *
     * @note The returned list does not include private channels or groups
     */
    ChannelList get_subscribed_channels() {
        return _bus.get_subscribed_channels(this, _storage);
    }

    /// Retrieves the list of groups subscribed by a specific listener.
    /**
     * This function returns a list of groups to which the specified listener is currently subscribed.
     *
     * @return A list of channel IDs representing the groups to which the listener is subscribed.
     *         The list will be empty if the listener has no active subscriptions.
     */
    ChannelList get_subscribed_groups() {
        return _bus.get_subscribed_groups(this, _storage);
    }

    ///Announces the presence of the specified listener on the network.
    /**
     * @param chan Name of unicast channel. This channel becomes available
     * for all nodes and any message send to this channel is forwarded
     * to this listener. Note that messages are sent as broadcast
     *
     * @note To maintain up-to-date routing information, this function should be invoked periodically,
     *       with an interval of at least one minute between calls.
     */
    void announce(ChannelID name, ConversationID reqid) {
        return _bus.announce(this, reqid, name);
    }


    ///Send serialized param pack
    /**
     * @param name name of channel
     * @param cid conversation id
     * @param args parameter pack. Basic types are only supported such a
     *   - integral types
     *   - trivial copyable types
     *   - strings / string_views
     *   - tuple
     *   - variant
     *   - optional
     *   - containers, such a vector, map, unordered_map, etc..
     *   - custom types must define Serializer<T> template specialization
     * @return
     */
    template<typename ... Args>
    bool send_pack(ChannelID name, ConversationID cid, const Args & ... args) {
        std::string buff;
        auto iter = std::back_inserter(buff);
        auto dummy = [](const auto &...) {};
        dummy(iter = bin::Serializer<Args>::srl(args)...);
        return send_message(name, buff, cid);
    }

protected:
    Bus _bus;
    ChannelListStorage _storage;
};






template<ClientCallback Callback>
class CallbackClient : public Client {
public:
    CallbackClient(Bus bus, Callback &&cb)
        :Client(std::move(bus)), _cb(std::forward<Callback>(cb)) {}

    virtual void on_close_group(ChannelID group_name) noexcept override {
        if constexpr(std::invocable<Callback, Client &, const GroupClosed &>) {
            _cb(*this, static_cast<const GroupClosed &>(group_name));
        }
    }
    virtual void on_delivery_error(const Undelivered &msg ) noexcept override{
        if constexpr(std::invocable<Callback, Client &, const Undelivered &>) {
            _cb(*this, msg);
        }
    }
    virtual void on_group_empty(ChannelID group_name) noexcept override{
        if constexpr(std::invocable<Callback, Client &, const GroupEmpty &>) {
            _cb(*this, static_cast<const GroupEmpty &>(group_name));
        }
    }
    virtual void on_message(const Message &message) noexcept override{
        if constexpr(std::invocable<Callback, Client &, const ChannelMessage &>) {
            _cb(*this, static_cast<const ChannelMessage &>(message));
        }
    }
    virtual void on_direct_message(const Message &message) noexcept override{
        if constexpr(std::invocable<Callback, Client &, const DirectMessage &>) {
            _cb(*this, static_cast<const DirectMessage &>(message));
        }
    }
    virtual void on_add_to_group(ChannelID group_name, ChannelID, ConversationID cid) noexcept override{
        if constexpr(std::invocable<Callback, Client &, const AddedToGroup &>) {
            _cb(*this, AddedToGroup{group_name, cid});
        }

    }

protected:
    Callback _cb;
};


template<ClientCallback Callback>
auto Bus::new_client(Callback &&cb) {
    return CallbackClient(*this, std::forward<Callback>(cb));
}
template<ClientCallback Callback>
std::unique_ptr<Client> Bus::new_client_unique(Callback &&cb) {
    return std::unique_ptr<CallbackClient<Callback> >(*this, std::forward<Callback>(cb));
}
template<ClientCallback Callback>
std::shared_ptr<Client> Bus::new_client_shared(Callback &&cb) {
    return std::shared_ptr<CallbackClient<Callback> >(*this, std::forward<Callback>(cb));

}



}

