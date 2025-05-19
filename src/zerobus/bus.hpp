#pragma once
#include "listener.hpp"
#include "channel_list_storage.hpp"
#include "channel_notify_listener.hpp"

#include <memory>
#include <span>
#include <vector>

namespace zerobus {

using SerialID = std::string;

struct SerialStatus {
    SerialID serial;
    const IListener *source = nullptr;
};

enum class UpdateSerialStatus {
    changed,
    not_changed,
    same,
    cycle
};


class IBus {
public:


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


    virtual ~IBus() = default;

    virtual bool subscribe(IListener *listener, ChannelID channel) = 0;
    virtual bool subscribe(IListener *listener, ChannelList channel) = 0;
    virtual void unsubscribe(IListener *listener, ChannelID channel) = 0;
    virtual void unsubscribe(IListener *listener, ChannelList channel) = 0;
    virtual void unsubscribe_all(IListener *listener) = 0;
    virtual void close_private_channel(IListener *listener) = 0;
    virtual bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) = 0;
    virtual void close_group(IListener *owner, ChannelID group_name) = 0;
    virtual void close_all_groups(IListener *owner) = 0;
    virtual bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) = 0;
    virtual bool forward_message(IListener *sender, const Message &msg) = 0;
    virtual std::string get_random_channel_name(std::string_view prefix) const = 0;
    virtual bool is_channel(ChannelID id) const = 0;
    virtual ChannelList get_public_channels(IListener *listener, ChannelListStorage &storage) const = 0;
    virtual ChannelList get_subscribed_channels(IListener *listener, ChannelListStorage &storage) const = 0;
    virtual ChannelList get_subscribed_groups(IListener *listener, ChannelListStorage &storage) const = 0;
    virtual void channel_notify(IChannelNotifyListener *mon, bool enable)  = 0;
    virtual void announce(IListener *listener, ConversationID req_id, ChannelID chan = {}) = 0;
    virtual void clear_path( ChannelID sender, ChannelID receiver, ConversationID cid) = 0;
    virtual SerialStatus get_serial() const = 0;
    virtual UpdateSerialStatus update_serial(IListener *lsn, const SerialID &serialId) = 0;

};

class Bus;

///Abstract client - associates bus with the listener
/**
 * Automatically unregisters itself when destroyed
 */
class AbstractClient: public IListener {
public:

    AbstractClient(std::shared_ptr<IBus> bus):_bus(std::move(bus)) {}
    ~AbstractClient() {
        _bus->unsubscribe_all(this);
    }
    Bus get_bus() const;


    bool subscribe(ChannelID channel) {
        return _bus->subscribe(this, channel);
    }
    void unsubscribe(ChannelID channel) {
        _bus->unsubscribe(this, channel);
    }
    void close_private_channel() {
        _bus->close_private_channel(this);
    }
    bool add_to_group(ChannelID group_name, ChannelID uid) {
        return _bus->add_to_group(this, group_name, uid);
    }
    void close_group(ChannelID group_name) {
        _bus->close_group(this, group_name);
    }
    void close_all_groups() {
        _bus->close_all_groups(this);
    }
    bool send_message(ChannelID channel, MessageContent msg, ConversationID cid = 0) {
        return _bus->send_message(this, channel, msg, cid);
    }
    bool forward_message(const Message &msg) {
        return _bus->forward_message(this, msg);
    }
    void announce(ConversationID req_id) {
        return _bus->announce(this, req_id);
    }


protected:
    std::shared_ptr<IBus> _bus;

};




class Bus {
public:
    ///create new bus;
    static Bus create();


    Bus(std::shared_ptr<IBus> handle):_ptr(std::move(handle)) {}

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
    bool subscribe(IListener *listener, ChannelID channel) {
        return _ptr->subscribe(listener, channel);
    }

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
    bool subscribe(IListener *listener, ChannelList channel) {
        return _ptr->subscribe(listener, channel);
    }

    /// Unsubscribes a listener from a specific channel.
    /**
     * This method removes the provided listener from the specified channel.
     * If the listener was not subscribed to the channel, the method does nothing.
     * The operation is guaranteed not to fail.
     *
     * @param listener Pointer to the listener object to be unsubscribed.
     * @param channel The ID of the channel to unsubscribe from.
     */
    void unsubscribe(IListener *listener, ChannelID channel) {
        _ptr->unsubscribe(listener, channel);
    }

    /// Unsubscribe multiple channels
    /**
     * @param listener listener
     * @param channels list of channels
     */
    void unsubscribe(IListener *listener, ChannelList channel) {
        _ptr->unsubscribe(listener, channel);
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
     * @param listener Pointer to the listener object to be unsubscribed.
     *
     * @note This function blocks until all pending messages are processed and
     * delivered. During this period, the listener may still receive messages
     * sent from other threads.
     */
    void unsubscribe_all(IListener *listener) {
        _ptr->unsubscribe_all(listener);
    }

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
     * may be returned to the sender through on_no_route().
     */
    void close_private_channel(IListener *listener) {
        _ptr->close_private_channel(listener);
    }


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
    bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) {
        return _ptr->add_to_group(owner, group_name, uid);
    }

    ///Closes the group, removes all members
    /**
     * @param owner pointer identifies the owner
     * @param group_name name of group to close
     */
    void close_group(IListener *owner, ChannelID group_name) {
        _ptr->close_group(owner, group_name);
    }

    ///Close all groups associated with the listener's pointer
    /**
     * @param owner pointer identifies the owner
     *
     * @note you don't need to call this function if unsubscribe_all() is
     * eventually used
     */
    void close_all_groups(IListener *owner) {
        _ptr->close_all_groups(owner);
    }

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
     *
     * @retval true    The message was successfully sent. Note that this does not guarantee delivery.
     *
     * @retval false   The message could not be sent due to various reasons, such as the target
     *                 channel not existing, missing routing information for the target channel,
     *                 or the channel being a group where the listener is not the owner.
     *
     * @note If the specified channel is a private channel that has already been closed, the function
     *       may still return true. However, the listener may asynchronously receive an error through
     *       the `on_no_route()` callback.
     */
    bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid = 0) {
        return _ptr->send_message(listener, channel, msg, cid);
    }

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
    bool forward_message(IListener *sender, const Message &msg) {
        return _ptr->forward_message(sender, msg);
    }

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
    std::string get_random_channel_name(std::string_view prefix) const {
        return _ptr->get_random_channel_name(prefix);
    }

    ///Determines whether id is a channel
    /**
     * @retval true id is channel
     * @retval flase id either doesn't exist or is not channel. Groups are not included
     */
    bool is_channel(ChannelID id) const {
        return _ptr->is_channel(id);
    }

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
    ChannelList get_subscribed_channels(IListener *listener, ChannelListStorage &storage) const {
        return _ptr->get_subscribed_channels(listener, storage);
    }

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
    ChannelList get_subscribed_groups(IListener *listener, ChannelListStorage &storage) const {
        return _ptr->get_subscribed_groups(listener, storage);
    }

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
    void channel_notify(IChannelNotifyListener *listener, bool enable) {
        _ptr->channel_notify(listener, enable);
    }

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
    ChannelList get_public_channels(IListener *skip_listener, ChannelListStorage &storage) const {
        return _ptr->get_public_channels(skip_listener, storage);
    }


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
     *      and calls the on_no_route() function on the bridge that
     *       receives messages for the sender. The bridge should forward
     *      the information to the other side, which should call
     *      this method to clean up the information on its side.
     *      If the sender is on the local bus, the on_no_route()
     *      function is called directly on the sender instance.
     */
    void clear_path(ChannelID sender, ChannelID receiver, ConversationID cid) {
         _ptr->clear_path( sender, receiver, cid);
    }

    ///Retrieves serial ID of whole network
    /**
     * This function is used to find the master node and detect cycles.
     * The function returns the current serial ID of the entire network.
     * During the initial setup, the value may change. After stabilization,
     * all nodes in the network will start returning this ID.
     * The value of the ID itself is a random unique string
     */
    SerialStatus get_serial() const {
        return _ptr->get_serial();
    }


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
    void announce(IListener *lsn, ConversationID reqid, ChannelID chan = {}) {
        return _ptr->announce(lsn, reqid, chan);
    }

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
    UpdateSerialStatus update_serial(IListener *lsn, const SerialID &serialId) {
        return _ptr->update_serial(lsn, serialId);
    }

    ///Retrieves pointer to underlying object
    auto get_handle() const {return _ptr;}

    ///Indicates that group has been closed
    /**
     * This constant is used to notify events for new_client() function.
     * In this case, sender contains name of group which has been closed
     */
    static constexpr MessageContent group_close = "c";
    ///Indicates that group is empty.
    /**
     * This constant is used to notify events for new_client() function.
     * In this case, sender contains name of the group. This client
     * is owner of the group. The event indicates that there is nobody
     * listening on the group
     */
    static constexpr MessageContent group_empty = "e";

    ///Indicates that this client has been added to group
    /**
     * This constant is used to notify events for new_client() function.
     * In this case, sender contains name of the group. It
     * indicates, that this clien has been added to the specified group
     * and now is able to receive messages broadcasted on that group.
     * These messages are flagged as public (not private)
     */
    static constexpr MessageContent group_add = "a";
    ///Indicates is known that some message was not delivered
    /**
     * This constant is used to notify events for new_client() function.
     * In this case, sender contains ID of the failed message's original
     * receiver. The conversiation ID is also filled with
     * conversation ID of original message. This indicates, that
     * message was not delivered, because there was no route information.
     *
     */
    static constexpr MessageContent no_route = "r";


    ///Construct ad-hoc client which call a function for every received event
    /**
     * The function receives pointer to associated instance of AbstractClient,
     * the message itself and flag , which indicates whether the message
     * is private. If tge flag is true, then message is private, otherwise
     * it is sent from public channel
     *
     * @param callback the callback function
     *
     * @note This function introduces a special channel to forward
     * non-message events. This is introduces for this case only. If the
     * flag is true, indicating that message is private, and channel of
     * the message is empty - which is otherwise impossible - then
     * sender contains source of the event and content contains type
     * of event. There are several types of events: group_close, group_empty,
     * group_add, no_route
     *
     * @code
     * new_client([&](AbstractClient *me, const Message &msg, bool pm){
     *  if (pm)  { //private message
     *      if (msg.get_channel().empty()) { // other event
     *          auto event = msg.get_content();
     *          if (event == Bus::no_route) {...}// mesage was not delivered
     *      }
     *  }
     * });
     *
     *
     * @return instance of the client (directly initialized). Note
     * the instance is not movable. If you need to create pointer, use
     * new_client_unique() or new_client_shared()
     */
    template<std::invocable<AbstractClient *, const Message &, bool> Callback>
    auto new_client(Callback &&callback) {

        class CbLsn: public AbstractClient {
        public:
            CbLsn(Callback &&cb, std::shared_ptr<IBus> bus)
                :AbstractClient(std::move(bus)),_cb(std::move(cb)) {}

            virtual void on_message(const Message &message, bool pm) noexcept override {
                _cb(this, message, pm);
            }
            virtual void on_close_group(zerobus::ChannelID group_name) noexcept override {
                _cb(this, Message(group_name, "", group_close,0), true);
            }
            virtual void on_add_to_group(ChannelID group_name, ChannelID ) noexcept override {
                _cb(this, Message(group_name, "", group_add,0), true);
            }
            virtual void on_group_empty(ChannelID group_name) noexcept override {
                _cb(this, Message(group_name, "", group_empty,0), true);
            }
            virtual void on_no_route(ChannelID, ChannelID receiver, ConversationID cid) noexcept override {
                _cb(this, Message(receiver, "", no_route, cid), true);
            }

        protected:
            std::decay_t<Callback> _cb;
        };

        return CbLsn(std::move(callback), _ptr);
    }

    ///Creates simple client as unique pointer
    /**
     * @param callback callback. For more information see new_client()
     * @return unique pointer to AbstractClient interface
     */
    template<std::invocable<AbstractClient *, const Message &, bool> Callback>
    std::unique_ptr<AbstractClient> new_client_unique(Callback &&callback) {
        return std::unique_ptr<AbstractClient>(new auto(new_client(std::move(callback))));
    }

    ///Creates simple client as shared pointer
    /**
     * @param callback callback. For more information see new_client()
     * @return shared pointer to AbstractClient interface
     */
    template<std::invocable<AbstractClient *, const Message &, bool> Callback>
    std::shared_ptr<AbstractClient> new_client_shared(Callback &&callback) {
        using Ret = decltype(this->new_client(std::move(callback)));
        return std::make_shared<Ret>(std::move(callback), _ptr);
    }



protected:
    std::shared_ptr<IBus> _ptr;

};

inline Bus AbstractClient::get_bus() const {
    return Bus(_bus);
}


}

