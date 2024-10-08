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

        virtual void send_message(const Message &msg) noexcept override;
        virtual void send_channels(const ChannelList &channels, Operation op) noexcept override;
        virtual void on_channels_update() noexcept override;
        virtual void send_reset() noexcept override;
        virtual void cycle_detection(bool state) noexcept override;
        virtual void on_close_group(ChannelID group_name)noexcept  override;
        virtual void on_clear_path(ChannelID sender, ChannelID receiver)noexcept  override;
        virtual void on_add_to_group(ChannelID group_name, ChannelID target_id)noexcept  override;

    protected:
        DirectBridge &_owner;
        std::vector<ChannelID> _pchns;
        std::vector<char> _pchrs;

    };


    Bridge _b1;
    Bridge _b2;
    bool _connected = false;

    Bridge &select_other(const Bridge &other);

    virtual void on_update_chanels(const Bridge &source, const Bridge::ChannelList &channels, Bridge::Operation op);
    virtual void on_message(const Bridge &source, const Message &msg);
    virtual void send_reset(const Bridge &source);
    virtual void cycle_detection(const Bridge &, bool ) noexcept {}
    virtual void on_close_group(const Bridge &source, ChannelID group_name);
    virtual void on_clear_path(const Bridge &source, ChannelID sender, ChannelID receiver);
    virtual void on_add_to_group(const Bridge &source, ChannelID group_name, ChannelID target_id);

};


}
