#pragma once

#include "listener.h"

namespace zerobus {

///Broker monitoring
/** This class is important to implement forwarding nodes */
class IMonitor {
public:
    virtual ~IMonitor() = default;
    ///notifies that list of channels has been updated
    /** Called under lock. You should send a notify to a processing thread
     *  to broadcast a new list of channels. You can use get_active_channels in that thread */
    virtual void on_channels_update() noexcept = 0;
    ///notifieas that a message to a channel has been dropped because channel doesn't exist
    /** Called under lock. You can send a message to a peer, that nobody is listening
     * @param lsn sender (listener) - can be nullptr
     * @param msg message
     * @retval true message has been processed
     * @retval false message was not processed (indicate to caller)
     *
     * */
    virtual bool on_message_dropped(IListener *lsn, const Message &msg) noexcept = 0;
};

}
