#pragma once

#include "utils/inline_function.hpp"
#include "message.hpp"

#include <chrono>
#include <memory>
#include <span>
#include <vector>

namespace zerobus {

class LocalBus;
class Terminal;
class IListener;
class IChannelNotifyListener;
class ChannelListStorage;
class Undelivered;

using SerialID = std::string;


struct SerialStatus { // @suppress("Miss copy constructor or assignment operator")
    ///contains current serial
    SerialID serial;
    ///contains pointer of sender who is responsible to set this serial
    const IListener *source = nullptr;
};

enum class UpdateSerialStatus {
    ///serial has been changed (adapted)
    changed,
    ///serial hasn't been changed (this node won)
    not_changed,
    ///serial is same
    same,
    ///serial is same, cycle detected
    cycle
};


/// Notification that the recipient has been added to a group
/**
 * The value contains the name of the group.
 */
struct AddedToGroup {
    ChannelID group_name;
    ConversationID cid;
};
/// Notification that a group the recipient was a member of has been closed
/**
 * * The value contains the name of the group.
 */
class GroupClosed: public ChannelID {};
/// Notification that a group has become empty (last member left)
/**
 * * The value contains the name of the group.
 * Sent to the group owner when the last member leaves.
 */
class GroupEmpty: public ChannelID {};
/// Message was broadcasted to a channel or group
/**
 * The "channel" property contains the name of the channel or group
 * where the message was sent.
 *
 *
 */
class ChannelMessage: public Message {};
/// Direct (private) message
/**
 * This message was sent directly to the recipient using their
 * unicast address. The channel property contains this unicast
 * address
 *
 * @note Messages sent to unicast address announced by
 * the function `announce()` are delivered as ChannelMesage
 */
class DirectMessage: public Message {};

template<typename Fn>
concept TerminalCallback =   std::is_invocable_v<Fn, Terminal &, const Undelivered &>
                        || std::is_invocable_v<Fn, Terminal &, const ChannelMessage &>
                        || std::is_invocable_v<Fn, Terminal &, const DirectMessage &>
                        || std::is_invocable_v<Fn, Terminal &, const GroupEmpty &>
                        || std::is_invocable_v<Fn, Terminal &, const GroupClosed &>
                        || std::is_invocable_v<Fn, Terminal &, const AddedToGroup &>;





enum class ChannelType {
    ///indicates, that name is not used for any channel
    not_used,
    ///indicates, that name specifies public channel
    /**
     * Multicast channel: anybody can subscribe, anybody can send message
     */
    multicast_channel,
    ///indicates, that name specifies local private channel
    /**
     * Private channel: one subscribed, anybody can send message
     */
    private_channel,
    ///indicates, that name specifies a group
    /**
     * Group: owner can post and add members
     */
    group,
};


class Bus {
public:
    ///create new bus;
    static Bus create();


    Bus(std::shared_ptr<LocalBus> handle);

    /// Subscribes a listener to a specific channel.
    /**
     * This function registers the provided listener to receive notifications
     * from the specified channel.
     *
     * @param listener Pointer to the listener object to be subscribed.
     * @param channel The ID of the channel to subscribe to.
     * @retval true if the subscription was successful.
     * @retval false if the channel name is invalid or reserved.
     */
    bool subscribe(IListener *listener, ChannelID channel);

    /// Subscribes a listener to multiple channels.
    /**
     * This method allows registering a listener to a list of channels in bulk.
     * It returns `true` if all channels were successfully subscribed, and `false`
     * if one or more channels could not be subscribed. In case of failure, the caller
     * should use the `get_subscribed_channels` method to determine which channels
     * were successfully subscribed.
     *
     * @param listener Pointer to the listener to be subscribed.
     * @param channel List of channels to subscribe the listener to.
     * @retval true if all channels were successfully subscribed
     * @retval false otherwise.
     */
    bool subscribe(IListener *listener, ChannelList channel);

    /// Unsubscribes a listener from a specific channel.
    /**
     * This method removes the provided listener from the specified channel.
     * If the listener was not subscribed to the channel, the method does nothing.
     * The operation is guaranteed not to fail.
     *
     * @param listener Pointer to the listener object to be unsubscribed.
     * @param channel The ID of the channel to unsubscribe from.
     */
    void unsubscribe(IListener *listener, ChannelID channel);

    /// Unsubscribe multiple channels
    /**
     * @param listener listener
     * @param channels list of channels
     */
    void unsubscribe(IListener *listener, ChannelList channel);

    /// Unsubscribes the listener from everything
    /**
     * Unsubscribes the listener from all channels and ensures that there are no
     * remaining references to the listener within the bus system.
     *
     * This function must be called before the listener is destroyed to prevent
     * undefined behavior. It guarantees that all pending messages for the listener
     * are delivered before the listener is fully unsubscribed.
     *
     * @param listener Pointer to the listener object to be unsubscribed.
     *
     * @note This function blocks until all pending messages are processed and
     * delivered. During this period, the listener may still receive messages
     * sent from other threads.
     */
    void unsubscribe_all(IListener *listener);

    ///Creates private channel for the listener
    /**
     * @param listener newly created listener
     * @return new channel name for peer-to-peer messages
     * @note returns existing channel name if already created
     *
     * @note acquires exclusive lock
     *
     */
    ChannelID create_private_channel(IListener *listener);

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
    void close_private_channel(IListener *listener);


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
     * @param owner Pointer to the local listener that owns the group (or becomes
     *              the owner if the group is being created).
     * @param group_name The name of the group to which the remote listener is
     *                   being added.
     * @param uid The name of the private channel for the newly added remote
     *            listener (member).
     * @retval true The remote listener was successfully added to the group.
     * @retval false The remote listener could not be added. This may occur if the
     *               specified local listener does not own the group, the group
     *               name is reserved, or the group name is invalid.
     */
    bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid, ConversationID cid);

    ///Closes the group, removes all members
    /**
     * @param owner pointer identifies the owner
     * @param group_name name of group to close
     */
    void close_group(IListener *owner, ChannelID group_name);

    ///Close all groups associated with the listener's pointer
    /**
     * @param owner pointer identifies the owner
     *
     * @note you don't need to call this function if unsubscribe_all() is
     * eventually used
     */
    void close_all_groups(IListener *owner);

    ///Send message to a channel
    /**
     * Sends a message to a specified channel.
     *
     * @param listener Pointer to the listener acting as the sender. This can be nullptr,
     *                 which sends an anonymous message. Anonymous messages cannot be replied to.
     *                 If a valid listener is provided, the target can send a reply as a direct
     *                 message to this listener.
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
     * @param importance Specifies message importance, see MsgFlags for list of options

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
    bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid = 0, MsgFlags flags = MsgFlags::priorityNormal);
    ///Forward message
    /**
     * Forwards a message to its intended recipient using the bus's routing system.
     *
     * This function is primarily designed for use by bridges. When an incoming bridge
     * receives an external message, it can use this function to forward the message
     * to its target listener. The Bus instance utilizes its routing information to
     * deliver the message to the appropriate listener or to an outgoing bridge.
     *
     * @param sender A pointer to the listener acting as the outgoing bridge. This
     * allows the Bus instance to store a return path for potential replies. This
     * value can be nullptr, if no return path need to be stored.
     *
     * @param msg A message to be forwarded. Use std::move() to avoid copy
     *
     * @retval true The message was successfully forwarded. Note this status is
     * also returned, if the message is forwarded asynchronously. There is no
     * way to determine, whether the message was delivered.
     * @retval false The message could not be forwarded due to missing routing information
     * or an unknown target. In such cases, the bridge should notify its counterpart
     * about the failed delivery by sending an error message.
     * @note the function doesn't perform checks for valid targets like
     * send_message(), so the function can return true even if the target
     * is known as unavailable.
     *
     */
    bool forward_message(IListener *sender, const Message &msg);

    ///Generates a random channel name
    /**
     * Generates a random channel name with the specified prefix.
     *
     * This function creates a unique channel name by appending a random
     * string with sufficient entropy to the provided prefix. The resulting
     * name can be used as a unique identifier for a channel.
     *
     * @param prefix A string prefix to prepend to the generated channel name.
     * @return A unique channel name with the specified prefix.
     */
    static std::string get_random_channel_name(std::string_view prefix);

    ///Determines whether id is a channel
    /**
     * @retval true id is channel
     * @retval flase id either doesn't exist or is not channel. Groups are not included
     */
    bool is_channel(ChannelID id) const;


    ///Determines, whether group exists and is owned by an owner
    /**
     * @param owner owner of group
     * @param group_id name of group
     * @retval true group exists and it is owned by owner
     * @retval false group doesn't exist, it is not group, or it is not owned by this owner
     */
    bool is_group(IListener *owner, ChannelID group_id) const;
    ///Determines type of channel
    /**
     * @param id name of channel
     * @return see ChannelType
     */
    ChannelType get_channel_type(ChannelID id) const;

    /// Retrieves the list of channels subscribed by a specific listener.
    /**
     * This function returns a list of channels to which the specified listener is currently subscribed.
     * The caller must provide a storage object to hold any underlying information referenced by the
     * returned value. The storage object ensures that the returned list remains valid for the duration
     * of its lifetime.
     *
     * @param listener A pointer to the listener whose subscriptions are being queried.
     *                 If the listener is not subscribed to any channels, the returned list will be empty.
     *
     * @param storage A reference to a storage object that will hold the underlying data
     *                required to maintain the validity of the returned list. The caller
     *                must ensure that this storage object remains in scope as long as
     *                the returned list is being used.
     *
     * @return A list of channel IDs representing the channels to which the listener is subscribed.
     *         The list will be empty if the listener has no active subscriptions.
     *
     * @note The returned list does not include private channels or groups
     */
    ChannelList get_subscribed_channels(IListener *listener, ChannelListStorage &storage) const;

    /// Retrieves the list of groups subscribed by a specific listener.
    /**
     * This function returns a list of groups to which the specified listener is currently subscribed.
     * The caller must provide a storage object to hold any underlying information referenced by the
     * returned value. The storage object ensures that the returned list remains valid for the duration
     * of its lifetime.
     *
     * @param listener A pointer to the listener whose subscriptions are being queried.
     *                 If the listener is not subscribed to any channels, the returned list will be empty.
     *
     * @param storage A reference to a storage object that will hold the underlying data
     *                required to maintain the validity of the returned list. The caller
     *                must ensure that this storage object remains in scope as long as
     *                the returned list is being used.
     *
     * @return A list of channel IDs representing the groups to which the listener is subscribed.
     *         The list will be empty if the listener has no active subscriptions.
     */
    ChannelList get_subscribed_groups(IListener *listener, ChannelListStorage &storage) const;

    ///enable or disable notification about changes in public channel list
    /**
     * Enables or disables notifications about changes in the public channel list.
     *
     * @param listener A pointer to the object that will receive notifications. Multiple listeners can
     *                 be registered to receive notifications simultaneously.
     * @param enable   If true, notifications are enabled for the specified listener. If false, notifications
     *                 are disabled for the specified listener.
     *
     * @note When notifications are enabled for multiple listeners, all of them will receive updates
     *       until notifications are explicitly disabled for each listener.
     */
    void channel_notify(IChannelNotifyListener *listener, bool enable);

    ///Retrieve all public channels, or channels not subscribed by specified listener
    /**
     * @param skip_listener This can be nullptr to return all public channels, or a valid
     * pointer to filter out channels subscribed by this listener. The use of the filter
     * is intended for bridges that do not want to report to their other side
     * the channels to which they themselves are subscribed.
     *
     * @param storage A reference to a storage object that will hold the underlying data
     *                required to maintain the validity of the returned list. The caller
     *                must ensure that this storage object remains in scope as long as
     *                the returned list is being used.
     *
     * @return A list of channel IDs representing the public channels
     */
    ChannelList get_public_channels(IListener *skip_listener, ChannelListStorage &storage) const;

    ///Deletes the path to the given recipient.
    /**
     * This function is used in a situation where a message cannot be delivered
     *  to the recipient because the recipient no longer exists.
     *  Once such a situation is detected, the bridge must call this function
     * to clear the path to the recipient. This also passes the information
     * to other bridges on the path all the way to the sender,
     *  who will then learn that the recipient is no longer available.
     *
     * @param sender id of sender of failed message
     * @param receive id of received of failed message
     *
     * @retval true path cleared, and request forwarded
     * @retval false
     *
     * @note The function deletes the recipient ID from the routing table
     *      and calls the on_delivery_error() function on the bridge that
     *       receives messages for the sender. The bridge should forward
     *      the information to the other side, which should call
     *      this method to clean up the information on its side.
     *      If the sender is on the local bus, the on_delivery_error()
     *      function is called directly on the sender instance.
     */
    void delivery_error(const Undelivered &msg);

    ///Retrieves serial ID of whole network
    /**
     * This function is used to find the master node and detect cycles.
     * The function returns the current serial ID of the entire network.
     * During the initial setup, the value may change. After stabilization,
     * all nodes in the network will start returning this ID.
     * The value of the ID itself is a random unique string
     */
    SerialStatus get_serial() const;


    ///Announces the presence of the specified listener on the network.
    /**
     * @param lsn   Pointer to the listener to be announced.
     * @param reqid Unique identifier of the request.
     * @param chan  (Optional) Name of a private channel. Used if the listener acts as a bridge and forwards the request to another part of the network.
     * Repeatedly call this function with a period of at least 1 minute.
     * This function ensures that all nodes in the network will eventually know the shortest path
     * to the specified listener, even in the presence of cycles in the network topology.
     *
     * @note To maintain up-to-date routing information, this function should be invoked periodically,
     *       with an interval of at least one minute between calls.
     */
    void announce(IListener *lsn, ConversationID reqid, ChannelID chan = {});

    ///Updates serial ID from the other node
    /**
     * Updates the serial ID on this node.
     * The new ID may or may not be accepted,
     * depending on the unified resolution function that decides whether the ID will be accepted.
     * The resulting ID should be obtained with the get_serial() function
     *
     * @param lsn A pointer to the listener instance that is also setting a new serial ID.
     *      The new ID, if accepted, is permanently associated with this instance.
     *      Therefore, if this function is used, the listener must call "unsubscribe_all"
     *      before it is destroyed to remove the association.
     * @param serialId a new serial ID
     * @retval true operation has been successful
     * @retval false failed, because cycle has been detected. The bridge identified as lsn should
     * immediately deactivate itself until the ID is changed
     *
     * Cycle detection works by remembering from which bridge a new ID was received.
     * If the same ID is received from another bridge, this is an indication of the
     * existence of a cycle. The bridge that detects a cycle in this way must deactivate itself
     * and only forward any updates to these IDs (there and back). If a new ID is received,
     * the cycle has been resolved and the bridge can be reactivated.
     */
    UpdateSerialStatus update_serial(IListener *lsn, const SerialID &serialId);


    ///Defer execution of the function outside of recursive context
    /**
     * The lambda function is executed ...
     *
     * 1) if the function is called inside of recursive context
     * the execution of the lambda is defered to end of current
     * recursive context
     *
     * 2) it is executed immediately otherwise.
     *
     * @param fn function to execute. Note the function must be movable
     */
    void defer(FunctionView<void()> fn);

    ///Creates new client
    /**
     * @param cb a callback function. It receives
     *
     * - reference to client's instance (this client)
     * - received message.
     *
     * The type of the second argument directly determines the message type.
     * If a function is to respond to different types of messages,
     * it must receive them all using "const auto &",
     * or use the `overloaded` template
     *
     * List of types
     *
     * - const Message & - receives both DirectMessage and ChannelMessage
     * - const DirectMessage &
     * - const ChannelMessage &
     * - const AddedToGroup &
     * - const GroupClosed
     * - const GroupEmpty &
     * - const Undelivered &
     *
     * @return instance of new client
     * @note requires #include <client.hpp>
     */
    template<TerminalCallback Callback>
    auto new_terminal(Callback &&cb);
    template<TerminalCallback Callback>
    std::unique_ptr<Terminal> new_terminal_unique(Callback &&cb);
    template<TerminalCallback Callback>
    std::shared_ptr<Terminal> new_terminal_shared(Callback &&cb);

    ///Changes routing TTL
    /**
     * Cache which holds informations about return paths has default timeout 10 minutes, after
     * which routing informations may be deleted. This function changes this value
     * for newly added and updated records
     *
     * @param ttl new ttl values
     */
    void set_routing_ttl(std::chrono::system_clock::duration ttl);

protected:
    std::shared_ptr<LocalBus> _ptr;
};



}

