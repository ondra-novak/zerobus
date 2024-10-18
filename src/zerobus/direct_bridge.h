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

    AbstractBridge &getBridge1();
    const AbstractBridge &getBridge1() const;
    AbstractBridge &getBridge2();
    const AbstractBridge &getBridge2() const;

protected:

    class Bridge: public AbstractBridge, public IMonitor { // @suppress("Miss copy constructor or assignment operator")
    public:
        Bridge(DirectBridge &owner, Bus &&b);
        virtual ~Bridge() override;

        virtual void on_channels_update() noexcept override;
        virtual void send(const Message &msg) noexcept override;
        virtual void send(const ChannelUpdate &msg) noexcept override;
        virtual void send(const ChannelReset &) noexcept override;
        virtual void send(const CloseGroup &msg) noexcept override;
        virtual void send(const ClearPath &msg) noexcept override;
        virtual void send(const AddToGroup &) noexcept override;
        virtual void send(const GroupEmpty&) noexcept override;
        virtual void send(const GroupReset&) noexcept override;
        virtual void send(const UpdateSerial&) noexcept override;

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

    virtual void on_send(const Bridge &source, const Bridge::ChannelUpdate &msg);
    virtual void on_send(const Bridge &source, const Message &msg);
    virtual void on_send(const Bridge &source, const Bridge::ChannelReset &msg);
    virtual void on_send(const Bridge &source, const Bridge::CloseGroup &msg);
    virtual void on_send(const Bridge &source, const Bridge::ClearPath &msg);
    virtual void on_send(const Bridge &source, const Bridge::AddToGroup &msg);
    virtual void on_send(const Bridge &source, const Bridge::GroupEmpty &msg);
    virtual void on_send(const Bridge &source, const Bridge::GroupReset &msg);
    virtual void on_send(const Bridge &source, const Bridge::UpdateSerial &msg);
    virtual void cycle_detection(const Bridge &, bool ) noexcept {}

};


}
