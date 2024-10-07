#pragma once
#include "message.h"

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
class IChannelFilter {
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
    virtual bool incoming(ChannelID id) const;
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
    virtual bool outgoing(ChannelID id) const;
    virtual ~IChannelFilter() = default;
};



}
