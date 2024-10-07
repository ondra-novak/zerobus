#pragma once

#include "message.h"

namespace zerobus {

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
};


}
