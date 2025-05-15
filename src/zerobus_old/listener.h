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


    ///Sent when there is no route to destination
    /**
     * @param sender who sent the message
     * @param receiver target of message
     *
     * The bridge forwards this event towards the `sender`. The `receive` contains ID of
     * receiver of failed message. For ordinary listener, the sender contains ID of this
     * listener, and receiver contains channel name previously send message, if it was
     * a direct message. There is no specification whether this message can appear
     * when message was sent to a channel or a group
     *
     * @note if the receiver was at the same node, this message doesn't appear. The
     * function send_message simply returns false. This message appear only when
     * message cannot be delivered after the send_message returned true
     */
    virtual void on_no_route(ChannelID sender, ChannelID receiver) noexcept  = 0;

    ///add group
    /**
     * Called when this listener was added to a group. If this listener is a bridge, this
     * event must be forwarded to the other side. Then the bridge calls add_to_group
     * on the other side.
     *
     * If this listener is an ordinary listener, this event notifies successfully add to
     * a group.
     *
     * @param group_name name of the group
     * @param target_id target id. For ordinary listener, there is ID of this listener
     */
    virtual void on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept = 0;

    ///close group
    /**
     * Called when group has been closed. The bridge forwards the event and calls
     * close_group() on the other side. The ordinary listener just receives notification
     * that group has been closed;
     *
     * @param group_name
     *
     */
    virtual void on_close_group(ChannelID group_name) noexcept= 0;

    ///called when last member of group left the group, so group is empty now
    /**
     * Members can leave group by calling unsubscribe with group name or
     * by unsubscribe_all. If this happen with a last member, this event is called.
     * Because there cannot be groups without members, this also means, that group
     * has been erased.
     *
     * The bridge forwards this event to other side, where it should unsubscribe the
     * group. An ordinary listener just receives notification, that his group has
     * no members.
     *
     * @param group_name name of group
     */
    virtual void on_group_empty(ChannelID group_name) noexcept = 0;
};


}
