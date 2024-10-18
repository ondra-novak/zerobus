#pragma once
#include "message.h"

#include "filter.h"
#include <atomic>
namespace zerobus {

///Abstract interface to implement channel filters
/**
 * This filter can be registered on a bridge. This allows to control which
 * channels can be forwarded to or from the other side
 *
 * You can filter only channels. Special channels for cycle detection cannot be
 * filtered (you should not use any channel with prefix !cdp_) You also cannot
 * peer to peer messages
 *
 */
class Filter {
public:

    ///Allow messages incoming (from external side) messages routed to given channel
    /**
     * @param id id of channel of incoming message - our channel
     * @retval true allow message
     * @retval false block message
     *
     * This function also causes that blocked channels are not subscribed
     * on their node
     */
    virtual bool on_incoming(ChannelID id) ;
    ///Allow messages outhoing (to external side) messages routed to given channel
    /**
     * @param id id of channel of outgoing channel - their channel
     * @param return_path is set to true, if id is return path - so message is response
     * @retval true allow message
     * @retval false block message
     *
     * This function also causes that blocked channels are not subscribed
     * on our node
     */
    virtual bool on_outgoing(ChannelID id) ;

    ///Called when group is created outside of node
    /**
     * needed to put group_name to a white list
     *
     * @param group_name name of the group
     * @param target_id id of target
     * @retval true allow message
     * @retval false block message
     */
    virtual bool on_incoming_add_to_group(ChannelID group_name, ChannelID target_id);

    ///Called when group is created inside of the node
    /**
     * needed to put group_name to a white list
     *
     * @param group_name name of the group
     * @param target_id id of target
     * @retval true allow message
     * @retval false block message
     */
    virtual bool on_outgoing_add_to_group(ChannelID group_name, ChannelID target_id);

    ///Called when external group is closed
    /**
     * @param group_name name of group
     * @retval true allow message
     * @retval false block message
     */
    virtual bool on_incoming_close_group(ChannelID group_name);

    ///Called when internal group is closed
    /**
     * @param group_name name of group
     * @retval true allow message
     * @retval false block message
     */
    virtual bool on_outgoing_close_group(ChannelID group_name);



    ///Retrieves flag signaling whether rules has been changed
    /**
     * @retval true rules has been changed
     * @retval false no rule changed
     *
     * @note function also clears this state
     *
     */
    bool commit_rule_changed() {
        return !_clean.test_and_set();
    }

    virtual ~Filter() = default;
protected:
    std::atomic_flag _clean = {};

    ///Sets flag signaling that filter rule changed
    /**
     * If filter operation causes changing in rules, use this function
     * to signal such state.
     */
    void set_rule_changed() {
        _clean.clear();
    }

};



}
