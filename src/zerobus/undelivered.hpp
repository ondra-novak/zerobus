#pragma once

#include "types.hpp"
#include "importance.hpp"

namespace zerobus {

/// Represents errors that can occur during message delivery.
enum class DeliveryError {
    /// No route to the target terminal or channel.
    /**
     * A transit node was unable to determine a route to the destination terminal or communication channel.
     */
    no_route = 0,

    /// Message discarded due to high traffic.
    /**
     * A message with low or normal importance was discarded because the high-water mark (HWM) limit was reached.
     */
    high_traffic = 1,

    /// Send operation timed out.
    /**
     * A high-importance message failed to be delivered within the timeout period.
     */
    send_timeout = 2,

    ///Target is invalid (message arrived to an internal service object or a bridge, where cannot be processed)
    invalid_target = 3,

    ///Attempt to add target to group while there is a name collision
    name_collision = 4,
};



class Undelivered {
public:
    ChannelID sender;
    ChannelID target;
    ConversationID cid;
    DeliveryError error;
    Importance importance;


};
}
