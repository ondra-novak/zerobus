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
};

}
