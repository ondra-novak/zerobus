#pragma once
#include "bus.h"
#include "bridge.h"

namespace zerobus {

///Implements direct bridge between two brokers
/**
 * This allows to connect two brokers directly. Messages sent to one broker are forwarded to other broker
 * and vice versa.
 *
 *
 * @note Do not create cycles!
 */
class DirectBridge {
public:

    ///ctor
    /**
     * @param b1 first broker
     * @param b2 second broker
     */
    DirectBridge(Bus b1, Bus b2);

protected:

    class Bridge: public AbstractBridge, public IMonitor { // @suppress("Miss copy constructor or assignment operator")
    public:
        Bridge(DirectBridge &owner, Bus &&b);
        virtual ~Bridge() override;

        virtual void on_message(const Message &message, bool pm) noexcept override;
        virtual void send_message(const Message &msg) noexcept override;
        virtual void send_channels(const ChannelList &channels) noexcept override;
        virtual void on_channels_update() noexcept override;
        virtual bool on_message_dropped(IListener *lsn, const Message &msg) noexcept override;

    protected:
        DirectBridge &_owner;

    };


    Bridge _b1;
    Bridge _b2;

    Bridge &select_other(const Bridge &other);

    void on_update_chanels(const Bridge &source, const Bridge::ChannelList &channels);
    void on_message(const Bridge &source, const Message &msg);
};


}
