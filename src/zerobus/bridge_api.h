#pragma once
#include "bus.h"
#include "monitor.h"

#include <exception>

namespace zerobus {

class IBridgeAPI : public IBus {
public:

    static constexpr std::string_view cycle_detection_prefix = "cdp_";

    using ChannelList = std::span<ChannelID>;

    ///Register channel monitor
    virtual void register_monitor(IMonitor *mon) = 0;
    ///Unregister channel monitor
    virtual void unregister_monitor(const IMonitor *mon) = 0;
    ///Retrieve active channels relative to listener
    /**
     * @param listener Listener from his point of view are obtained a list of channels.
     *                  Obviously, channels that are subscribed to this listener are skipped,
     *                  only if someone else is listening to the channel, then that channel is also included.
     *                  Private groups are excluded.
     *
     * @param storage object used as storage for channel data and makes return value valid. You need
     * to keep this object while you processing the result
     * @return list of channels. List is always ordered (std::less<std::string>)
     */
    virtual ChannelList get_active_channels(IListener *listener, ChannelListStorage &storage) const = 0;
    ///Unsubscribe all channels subscribed to this listener
    /**
     *
     * @param listener listener
     * @param and_groups set true to unsubscribe groups as well.
     */
    virtual void unsubscribe_all_channels(IListener *listener, bool and_groups) = 0;
    virtual bool dispatch_message(IListener *listener, const Message &msg, bool subscribe_return_path) = 0;
    ///retieve channel name used to detect cycles
    /**
     * @return name of channel which should not be subscribed. It is intended to detect
     * cyclces. The bridge should check incoming channels for this string. If the
     * string is found, the bridge knows, that it closes the cycle, so it should
     * temporarily disable its bridging function
     */
    virtual std::string_view get_cycle_detect_channel_name() const = 0;
    static std::shared_ptr<IBridgeAPI> from_bus(const std::shared_ptr<IBus> &bus) {
        return std::static_pointer_cast<IBridgeAPI>(bus);
    }

    ///Clears path to the sender
    /**
     * @param lsn bridge that was responsible to deliver the message
     * @param sender id of sender of the last message
     * @param receiver id of receiver if the last message
     * @retval true cleared
     * @retval false no such path
     */
    virtual bool clear_return_path(IListener *lsn, ChannelID sender, ChannelID receiver) = 0;

    ///calls on_update_channels on all monitors
    /**
     * This call can be performed asynchronously, or out of current scope. It
     * only guarantees that this call happen as soon as possible.
     */
    virtual void force_update_channels() = 0;

};

///exception is thrown when cycle is detected during subscribtion
class CycleDetectedException : public std::exception {
public:
    virtual const char *what() const noexcept override {return "zerobus: Cycle detected";}
};


}
