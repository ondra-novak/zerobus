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
    DirectBridge(Bus b1, Bus b2, bool connect_now);
    virtual ~DirectBridge() = default;

    void connect();

protected:

    class Bridge: public AbstractBridge, public IMonitor { // @suppress("Miss copy constructor or assignment operator")
    public:
        Bridge(DirectBridge &owner, Bus &&b);
        virtual ~Bridge() override;

        virtual void on_channels_update() noexcept override;
        virtual void send(Message &&msg) noexcept override;
        virtual void send(ChannelUpdate &&msg) noexcept override;
        virtual void send(ChannelReset &&) noexcept override;
        virtual void send(CloseGroup &&msg) noexcept override;
        virtual void send(ClearPath &&msg) noexcept override;
        virtual void send(AddToGroup &&) noexcept override;
        virtual void cycle_detection(bool state) noexcept override;

    protected:
        DirectBridge &_owner;
        std::vector<ChannelID> _pchns;
        std::vector<char> _pchrs;

    };


    Bridge _b1;
    Bridge _b2;
    bool _connected = false;

    Bridge &select_other(const Bridge &other);

    virtual void on_send(const Bridge &source, Bridge::ChannelUpdate &&msg);
    virtual void on_send(const Bridge &source, Message &&msg);
    virtual void on_send(const Bridge &source, Bridge::ChannelReset &&msg);
    virtual void on_send(const Bridge &source, Bridge::CloseGroup &&msg);
    virtual void on_send(const Bridge &source, Bridge::ClearPath &&msg);
    virtual void on_send(const Bridge &source, Bridge::AddToGroup &&);
    virtual void cycle_detection(const Bridge &, bool ) noexcept {}

};


}
