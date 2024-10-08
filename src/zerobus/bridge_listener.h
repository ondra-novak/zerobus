#pragma once

#include "listener.h"

namespace zerobus {

///Message listener
class IBridgeListener : public IListener{
public:

    ///clear path when receiver is no longer listening
    /**
     * @param sender who sent the message
     * @param receiver target of message
     *
     * One bridge instance can call other bridge instance to forward clear_path message
     * propagating the event from receiver to sender (follows the path)
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
     */
    virtual void on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept = 0;

    ///close group
    /**
     * Called by bus when close_group was called on given group
     * @param group_name
     */
    virtual void on_close_group(ChannelID group_name) noexcept= 0;

};


}
