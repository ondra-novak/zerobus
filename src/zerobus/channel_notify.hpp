#pragma once
#include "bus.hpp"
#include "channel_notify_listener.hpp"


#include <chrono>
#include <condition_variable>
#include <mutex>
namespace zerobus {


///invokes callback when channel becomes available
/**
 * @tparam CB callback function
 */
template<std::invocable<> CB>
class ChannelNotifyCallback: public IChannelNotifyListener {
public:

    ///constructor
    /**
     * @param bus bus instance
     * @param channel monitoring channel
     * @param cb callback which is called when channel becomes available
     */
    ChannelNotifyCallback(Bus bus, ChannelID channel, CB cb)
        :_bus(std::move(bus))
        ,_channel(channel)
        ,_cb(std::move(cb)) {
        _bus.channel_notify(this, true);
        ChannelNotifyCallback::on_channels_update();
    }

    ChannelNotifyCallback(const ChannelNotifyCallback &) = delete;
    ChannelNotifyCallback &operator=(const ChannelNotifyCallback &) = delete;

    ~ChannelNotifyCallback() {
        _bus.channel_notify(this, false);
    }


protected:
    Bus _bus;
    ChannelID _channel;
    CB _cb;

    virtual void on_channels_update() noexcept override {
        if (_bus.is_channel(_channel)) {
            _bus.channel_notify(this, false);
            _cb();
        }
    }

    virtual void on_announce(IListener *, zerobus::ConversationID,
                                zerobus::ChannelID) noexcept override {}

};

///wait until channel is available
/**
 * @param bus instance of Bus class
 * @param channel awaiting channel name
 * @param tp timeout timepoint
 * @retval true channel appeared
 * @retval false timeout
 */
bool channel_wait_until(Bus bus, ChannelID channel, std::chrono::system_clock::time_point tp) {
    std::mutex mx;
    std::condition_variable cond;
    bool signal = false;
    ChannelNotifyCallback cb(std::move(bus), channel, [&]{
        std::lock_guard _(mx);
        signal = true;
        cond.notify_all();
    });
    std::unique_lock lk(mx);
    return cond.wait_until(lk, tp, [&]{return signal;});
}

///wait until channel is available
/**
 * @param bus instance of Bus class
 * @param channel awaiting channel name
 * @param dur duration of wait
 * @retval true channel appeared
 * @retval false timeout
 */
bool channel_wait_for(Bus bus, ChannelID channel, std::chrono::system_clock::duration dur) {
    return channel_wait_until(std::move(bus), std::move(channel),
            std::chrono::system_clock::now()+dur);
}



}

