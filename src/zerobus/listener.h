#pragma once

#include "message.h"

namespace zerobus {

class IBridgeListener;

///Message listener
class IListener {
public:


    virtual ~IListener() = default;
    ///Internal virtual function which can retrieve bridge from this object
    /** Dynamic cast replacement for this special purpose */
    virtual IBridgeListener *get_bridge() noexcept {return nullptr;}

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
