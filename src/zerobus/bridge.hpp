#include "iprotocol.hpp"

#include "bus.hpp"

#include <atomic>
namespace zerobus {

class AbstractTransport: public IProtocol {
public:

    AbstractTransport() = default;
    AbstractTransport(const AbstractTransport &other) = delete;
    AbstractTransport &operator=(const AbstractTransport &other) = delete;

    virtual void set_target(IProtocol *target) = 0;

};

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


class Bridge: public IProtocol, public IListener, public IChannelNotifyListener {
public:

    Bridge(Bus bus, std::shared_ptr<AbstractTransport> transport, BridgeOpMode op = BridgeOpMode::bidirectional);
    ~Bridge();


    void send_reset();
    void send_new_session(unsigned long version);
    unsigned long get_version() const {return _version;}

    void set_mode(BridgeOpMode mode);
    BridgeOpMode get_mode() const;

protected:

    //IChannelNotifyListener
    virtual void on_channels_update() noexcept override;

    //IListener
    virtual void on_close_group(ChannelID group_name) noexcept override;
    virtual void on_no_route(ChannelID sender,ChannelID receiver, ConversationID cid) noexcept override;
    virtual void on_group_empty(ChannelID group_name) noexcept override;
    virtual void on_add_to_group(ChannelID group_name,ChannelID target_id) noexcept override;
    virtual void on_message(const Message &message, bool pm) noexcept override;

    //IProtocol
    virtual void on_message(const MsgMessage &msg) noexcept override;
    virtual void on_message(const MsgSetChannels &msg) noexcept override;
    virtual void on_message(const MsgAddChannels &msg) noexcept override;
    virtual void on_message(const MsgEraseChannels &msg) noexcept override;
    virtual void on_message(const MsgUpdateSerial &msg) noexcept override;
    virtual void on_message(const MsgChannelReset &msg) noexcept override;
    virtual void on_message(const MsgNewSession &msg) noexcept override;
    virtual void on_message(const MsgNoRoute &msg) noexcept override;
    virtual void on_message(const MsgCloseGroup &msg) noexcept override;
    virtual void on_message(const MsgGroupEmpty &msg) noexcept override;
    virtual void on_message(const MsgAddToGroup &msg) noexcept override;


protected:

Bus _bus;
    std::shared_ptr<AbstractTransport> _transport;

    std::string _serial_id;
    ChannelListStorage _cur_list;
    ChannelListStorage _tmp_list;
    ChannelListStorage _diff_list;
    unsigned long _version = 0;
    std::atomic<unsigned char> _lk_flag = {0};
    std::atomic<unsigned char> _cycle_status = {false};
    std::atomic<BridgeOpMode> _op_mode ={BridgeOpMode::bidirectional};
    static constexpr unsigned char chan_locked = 1;
    static constexpr unsigned char chan_need_update = 2;
    static constexpr unsigned char chan_need_reset = 4;


    void on_channels_update_lk() noexcept;


};


}

