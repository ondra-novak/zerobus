#pragma once
#include "protocol.hpp"
#include "bus.hpp"
#include "listener.hpp"
#include "channel_notify_listener.hpp"

#include "channel_list_storage.hpp"

#include <atomic>
namespace zerobus {


///defines basic bridge filtering
/**
 * This defines how messages sent to public channels are processed. This
 * filter doesn't affect private and group messages
 */
enum class BridgeOpMode {
    ///bidirectional mode
    /**
     * both sides propagates and routes to public channels
     */
    bidirectional,///< normal


    ///inbound mode
    /**
     * This allows that only inbound messages sent to public channels are
     * allowed (outbound are blocked)
     *
     * @note in this mode, channels anounced by other side are not propagated
     * to local bus
     */
    inbound,

    ///outbound mode
    /**
     * This allows that only outbound messages sent to public channels are
     * allowed (inbound are blocked)
     *
     * @note in this mode, channels anounced on local bus are not propagated
     * to other side
     */
    outbound,///< deaf


    ///No channels are propagated.
    /** Only peer-to-peer and groups are allowed. However, because you
     * need public channels to initiate peer-to-peer or to create group, this
     * mode causes that transport will be disabled if the option is set at the
     * beginning. However this allows to change mode later
     */
    isolated
};


class Bridge: public AbstractTransport, public IListener, public IChannelNotifyListener {
public:

    ///Init the bridge in master mode
    /**
     * @param bus associated bus which this side monitors
     * @param transport unique pointer to transport object, which performs
     *  serialization of the messages
     * @param op bridge operation mode
     *
     * The constructor performs bidirectional. The transport object is owned
     * by the bridge and it is destroyed with the Bridge
     */
    Bridge(Bus bus, std::unique_ptr<AbstractTransport> transport, BridgeOpMode op = BridgeOpMode::bidirectional);
    ///Initialize the bridge in slave mode
    /**
     * The bridge is still associated with the bus, but it is not connected
     * to a transport. You need to connect it before the bus is used to
     * send messages. To connect it, use set_target()
     *
     * @param bus associated bus
     * @param op bridge operation mode
     */
    Bridge(Bus bus, BridgeOpMode op = BridgeOpMode::bidirectional);

    ///Destroy bridge
    ~Bridge();

    ///Sets target for outgoing messages.
    /** Use this function to connect a slave bridge to its master */
    virtual void set_target(IProtocol *target) override;

    ///Send reset request
    /** The other side should respond with MsgSetChannels to update
     * list of channels
     */
    void send_reset();
    ///Initate a session, send version
    void send_new_session(unsigned long version);
    ///retrieve current version
    unsigned long get_version() const {return _version;}

    ///change bridge mode
    void set_mode(BridgeOpMode mode);
    ///get bridge mode
    BridgeOpMode get_mode() const;

    ///Retrieve information about cycle detection
    /**
     * @retval true cycle was detected and this bridge doesn't propagate
     * channels to the other side.
     * @return false cycle was not detected, normal operation
     */
    bool get_cycle_status() const {return _cycle_status.load(std::memory_order_relaxed);}

    ///Refresh list of channels
    /**
     * Ensures that all channels are propagated to the other side
     * @param force use MsgSetChannels to ensure that no channel is missing.
     * If this argument is false, the function can do nothing if there
     * is no change detection.
     */
    void refresh(bool force);

    ///Disconnect from the bus (unsubscribe all)
    void disconnect();

protected:

    //IChannelNotifyListener
    virtual void on_channels_update() noexcept override;
    virtual void on_announce(IListener *sender, ConversationID reqid, ChannelID chan) noexcept override;

    //IListener
    virtual void on_close_group(ChannelID group_name) noexcept override;
    virtual void on_delivery_error(const Undelivered &error) noexcept override;
    virtual void on_group_empty(ChannelID group_name) noexcept override;
    virtual void on_add_to_group(ChannelID group_name,ChannelID target_id, ConversationID cid) noexcept override;
    virtual void on_message(const Message &message) noexcept override;
    virtual void on_direct_message(const Message &message) noexcept override;

    //IProtocol
    virtual void receive(const Message &msg) noexcept override;
    virtual void receive(const bmsg::AddChannels &msg) noexcept override;
    virtual void receive(const bmsg::EraseChannels &msg) noexcept override;
    virtual void receive(const bmsg::UpdateSerial &msg) noexcept override;
    virtual void receive(const bmsg::ChannelReset &msg) noexcept override;
    virtual void receive(const bmsg::NewSession &msg) noexcept override;
    virtual void receive(const Undelivered &msg) noexcept override;
    virtual void receive(const bmsg::CloseGroup &msg) noexcept override;
    virtual void receive(const bmsg::GroupEmpty &msg) noexcept override;
    virtual void receive(const bmsg::AddToGroup &msg) noexcept override;
    virtual void receive(const bmsg::Announce &) noexcept override;


protected:

    Bus _bus;
    std::unique_ptr<AbstractTransport> _transport = {};
    IProtocol *_target = nullptr;

    std::string _serial_id;
    ChannelListStorage _cur_list;
    ChannelListStorage _tmp_list;
    std::vector<ChannelID> _diff_list;
    unsigned long _version = 0;
    std::atomic<unsigned char> _lk_flag = {0};
    std::atomic<bool> _cycle_status = {false};
    std::atomic<BridgeOpMode> _op_mode ={BridgeOpMode::bidirectional};
    static constexpr unsigned char chan_locked = 1;
    static constexpr unsigned char chan_need_update = 2;
    static constexpr unsigned char chan_need_reset = 4;


    void on_channels_update_lk(bool force) noexcept;


};


}

