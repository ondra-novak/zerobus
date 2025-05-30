#pragma once

#include "types.hpp"
#include "message_flags.hpp"

namespace zerobus {

/// Represents errors that can occur during message delivery.
enum class DeliveryError {
    ///this value is not used (no delivery error specified)
    not_used = 0,
    /// No route to the target terminal or channel. (always sent)
    /**
     * A transit node was unable to determine a route to the destination terminal or communication channel.
     *
     */
    no_route = 1,

    /// Message discarded due to high traffic. (when notify)
    /**
     * A message with low or normal importance was discarded because the high-water mark (HWM) limit was reached.
     *
     * @note This error can appear only when MsgFlags is set with notify
     */
    high_traffic = 2,

    /// Send operation timed out. (when notify)
    /**
     * A high-importance message failed to be delivered within the timeout period.
     *
     * @note This error can appear only when MsgFlags is set with notify
     *
     */
    send_timeout = 3,

    ///Target is invalid (message arrived to an internal service object or a bridge, where cannot be processed)
    invalid_target = 4,

    ///Attempt to add target to group while there is a name collision
    name_collision = 5,

    ///Attempt to send message to disconnected route
    /**
     * @note This error can appear only when MsgFlags is set with notify
     */
    not_connected = 6
};



class Undelivered {
public:
    ChannelID sender;
    ChannelID target;
    ConversationID cid;
    DeliveryError error;
    MsgFlags flags;


};
}
