#pragma once

#include "message.h"

namespace zerobus {

class IListener;

///Message listener
class IListener {
public:


    virtual ~IListener() = default;

    ///Message received
    /**
     * @param message contains message
     * @param pm this is set to true, if message was sent to private mailbox.
     * If this is set false, the message was sent to public channel. If listener
     * is bridge, this flag is true, when message is send to registered
     * return path.
     *
     *
     * @note if pm is set, channel name of the message is undefined
     */
    virtual void on_message(const Message &message, bool pm) noexcept= 0;


    ///clear path when receiver is no longer listening
    /**
     * @param sender who sent the message
     * @param receiver target of message
     *
     * One bridge instance can call other bridge instance to forward clear_path message
     * propagating the event from receiver to sender (follows the path)
     *
     * If this interface used as end point client, the function is called whenever the
     * message cannot be delivered to the receiver, because it is probably no longer available
     */
    virtual void on_clear_path(ChannelID sender, ChannelID receiver) noexcept  = 0;

    ///add group
    /**
     * Called by bus when add_to_group is called while following path to the target_id
     * @param group_name name of the group
     * @param target_id target id
     *
     * This function also means that the bridge was subscribed to the group
     *
     * The bridge should call add_to_group on target network. If the path is not possible, it
     * should unsubscribe the bridge from the channel.
     *
     * If this interface is used for end point client, the function is called when
     * the client is added to a group. In this case, the group_name contains
     * name of the group and target_id contains id of this client
     */
    virtual void on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept = 0;

    ///close group
    /**
     * Called by bus when close_group was called on given group
     * @param group_name
     *
     * If this interface is used ro end point client, the function is called when
     * the group has been closed and the client was removed from the group
     */
    virtual void on_close_group(ChannelID group_name) noexcept= 0;

    ///called when last member of group left the group, so group is empty now
    /**
     * Members can leave group by calling unsubscribe with group name or
     * by unsubscribe_all. If this happen with last member, this event is called.
     * Because there cannot be groups without members, this also means, that group
     * has been erased.
     *
     * @param group_name name of group
     */
    virtual void on_group_empty(ChannelID group_name) noexcept = 0;
};


}
