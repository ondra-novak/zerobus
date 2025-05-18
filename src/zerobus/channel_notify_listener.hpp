#pragma once

namespace zerobus {

///  Interface for receiving notifications about channel updates.
/**
 * This interface defines a contract for objects that need to be notified
 * when there are changes in the list of channels. Implementers of this
 * interface should provide their own logic for handling such notifications.
 */
class IChannelNotifyListener {
public:
    /// Virtual destructor.
    virtual ~IChannelNotifyListener() = default;

    /// Notification about channel updates.
    virtual void on_channels_update() noexcept = 0;
};


}
