#pragma once
#include "bus.h"
#include "monitor.h"
#include "functionref.h"
#include "filter.h"

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
    ///Retrieve active channels relative to listener
    /**
     * @param listener the function skips channels subscribed by this listener. Use nullptr to show all channels
     * @param cb callback function
     *
     * @note Returned list should be in lexicographic order (std::less)
     */
    virtual void get_active_channels(IListener *listener, FunctionRef<void(ChannelList) > &&cb) const = 0;
    virtual void unsubscribe_all_channels(IListener *listener) = 0;
    virtual Message create_message(ChannelID sender, ChannelID channel, MessageContent msg, ConversationID cid) = 0;
    virtual bool dispatch_message(IListener *listener, Message &&msg, bool subscribe_return_path) = 0;
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

namespace Msg {

enum class Operation {
    ///replace subscribed channels with a new set
    replace = 0,
    ///subscribe new channels
    add = 1,
    ///unsubscribe channels
    erase = 2,
};


using ChannelList = IBridgeAPI::ChannelList;
struct ChannelUpdate {
    ChannelList lst;
    Operation op;
};

struct ChannelReset {};
struct ClearPath {
    ChannelID sender;
    ChannelID receiver;
};
struct CloseGroup {
    ChannelID group;
};

struct AddToGroup {
    ChannelID group;
    ChannelID target;
};


}

///Abstract bridge class. Extend this class to implement the bridge
class AbstractBridge: public IListener {
public:


    using Operation = Msg::Operation;
    using ChannelList = Msg::ChannelList;
    using ChannelUpdate = Msg::ChannelUpdate;
    using ChannelReset =  Msg::ChannelReset;
    using ClearPath = Msg::ClearPath;
    using CloseGroup = Msg::CloseGroup;
    using AddToGroup = Msg::AddToGroup;

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
    void receive(ChannelUpdate &&chan_up);

    void receive(ChannelReset);


    ///apply their clear path command
    void receive(ClearPath &&cp);

    void receive(CloseGroup &&msg) ;
    void receive(AddToGroup &&msg);

    void receive(Message &&msg);



    void register_monitor(IMonitor *mon) {
        _ptr->register_monitor(mon);
    }
    void unregister_monitor(const IMonitor *mon) {
        _ptr->unregister_monitor(mon);
    }

    void set_filter(std::unique_ptr<Filter> &&flt);


    Bus get_bus() const {return Bus(_ptr);}

    virtual void on_close_group(zerobus::ChannelID group_name) noexcept
            override;
    virtual void on_clear_path(zerobus::ChannelID sender,
            zerobus::ChannelID receiver) noexcept override;
    virtual void on_add_to_group(zerobus::ChannelID group_name,
            zerobus::ChannelID target_id) noexcept override;

protected:
    ///override - send channels to other side
    /**
     * @param channels list channels
     * @param op operation with channels
     */
    virtual void send(ChannelUpdate &&msg) noexcept = 0;
    virtual void send(Message &&msg) noexcept = 0;
    virtual void send(ChannelReset &&) noexcept = 0;
    virtual void send(CloseGroup &&) noexcept = 0;
    virtual void send(AddToGroup &&) noexcept = 0;
    virtual void send(ClearPath &&) noexcept = 0;

    virtual void process_mine_channels(ChannelList lst) noexcept;

    ///diagnostic override called when cycle detection state changed;
    virtual void cycle_detection(bool ) noexcept {};
protected:

    std::shared_ptr<IBridgeAPI> _ptr;

    std::vector<char> _char_buffer = {};
    std::vector<ChannelID> _cur_channels = {};
    std::vector<ChannelID> _tmp = {};   ///< temporary buffer for channel operations
    std::atomic<Filter *> _filter = {};
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
