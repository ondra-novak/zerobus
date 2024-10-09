#pragma once
#include "bus.h"
#include "monitor.h"
#include "functionref.h"
#include "filter.h"
#include "listener_bridge.h"

#include <span>
#include <vector>
#include <atomic>
namespace zerobus {

class IBridgeAPI : public IBus {
public:

    static constexpr std::string_view cycle_detection_prefix = "cdp_";

    using ChannelList = std::span<ChannelID>;

    virtual void register_monitor(IMonitor *mon) = 0;
    virtual void unregister_monitor(const IMonitor *mon) = 0;
    virtual void get_active_channels(IListener *listener, FunctionRef<void(ChannelList) > &&cb) const = 0;
    virtual Message create_message(ChannelID sender, ChannelID channel, MessageContent msg, ConversationID cid) = 0;
    virtual bool dispatch_message(IBridgeListener *listener, Message &&msg, bool subscribe_return_path) = 0;
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
    virtual bool clear_return_path(IBridgeListener *lsn, ChannelID sender, ChannelID receiver) = 0;

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


///Abstract bridge class. Extend this class to implement the bridge
class AbstractBridge: public IBridgeListener {
public:


    enum class Operation {
        ///replace subscribed channels with a new set
        replace,
        ///subscribe new channels
        add,
        ///unsubscribe channels
        erase,
    };


    using ChannelList = IBridgeAPI::ChannelList;

    AbstractBridge(Bus bus);

    virtual ~AbstractBridge();


    AbstractBridge(const AbstractBridge &other):_ptr(other._ptr) {}
    AbstractBridge &operator=(const AbstractBridge &other) = delete;

    auto get_handle() const {return _ptr;}

    ///Sends list of channels of current broker to the other side
    /**
     * Retrieves active list of channels from a connected broker and generates a list which is then
     * forwarded to the function on_channels_update(). It also detects changes in the list and skips
     * sending the list if no change detected
     *
     * @note @b mt-safety: this method is not mt-safe
     */
    void send_mine_channels();

    ///Apply list of channels of other/remote broker
    /**
     * The function subscribes new channels and unsubscribes no longer active channels by a list
     * received from other side.
     *
     * @param lst list of channels of other/remote broker. Note that argument is not const and can
     * be changed during processing (content is ordered)
     *
     * @note @b mt-safety: this method is mt-safe relative to other methods, but not mt-safe for calling
     * it from multiple threads
     *
     */
    void apply_their_channels(ChannelList lst, Operation op);

    ///apply their reset command on our side
    void apply_their_reset();

    ///apply their clear path command
    void apply_their_clear_path(ChannelID sender, ChannelID receiver);

    void apply_their_close_group(ChannelID group_name) ;
    void apply_their_add_to_group(ChannelID group_name, ChannelID target_id);


    ///Forward message from other side to connected broker
    /**
     * @param msg message to forward
     *
     * @note @b mt-safety: this method is mt-safe
     */
    void dispatch_message(Message &msg);
    void dispatch_message(Message &&msg);


    void register_monitor(IMonitor *mon) {
        _ptr->register_monitor(mon);
    }
    void unregister_monitor(const IMonitor *mon) {
        _ptr->unregister_monitor(mon);
    }

    void set_filter(std::unique_ptr<IChannelFilter> &&flt);


    Bus get_bus() const {return Bus(_ptr);}

protected:
    ///override - send channels to other side
    /**
     * @param channels list channels
     * @param op operation with channels
     */
    virtual void send_channels(const ChannelList &channels, Operation op) noexcept = 0;
    ///overeide - send message to other side
    virtual void send_message(const Message &msg) noexcept = 0;
    ///override - send reset command to other side
    virtual void send_reset() noexcept = 0;

    virtual void process_mine_channels(ChannelList lst) noexcept;

    ///diagnostic override called when cycle detection state changed;
    virtual void cycle_detection(bool ) noexcept {};
protected:

    std::shared_ptr<IBridgeAPI> _ptr;

    std::vector<char> _char_buffer = {};
    std::vector<ChannelID> _cur_channels = {};
    std::vector<ChannelID> _tmp = {};   ///< temporary buffer for channel operations
    std::size_t _chan_hash = 0;
    std::atomic<IChannelFilter *> _filter = {};
    bool _cycle_detected = false;


    static ChannelList persist_channel_list(const ChannelList &source, std::vector<ChannelID> &channels, std::vector<char> &characters);

    virtual void on_message(const Message &message, bool pm) noexcept override;


};

class AbstractMonitor: public IMonitor {
public:
    AbstractMonitor(AbstractBridge &b):_b(b) {
        _b.register_monitor(this);
    }
    ~AbstractMonitor() {
        _b.unregister_monitor(this);
    }
    AbstractMonitor(const AbstractMonitor &other):_b(other._b) {
        _b.register_monitor(this);
    }
    AbstractMonitor &operator=(const AbstractMonitor &other) = delete;

protected:
    AbstractBridge &_b;
};



}
