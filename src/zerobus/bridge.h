#pragma once
#include "bus.h"
#include "monitor.h"
#include "functionref.h"
#include <span>
#include <vector>
namespace zerobus {

class IBridgeAPI : public IBus {
public:

    using ChannelList = std::span<ChannelID>;

    virtual void register_monitor(IMonitor *mon) = 0;
    virtual void unregister_monitor(const IMonitor *mon) = 0;
    virtual void get_active_channels(IListener *listener, FunctionRef<void(ChannelList) > &&cb) const = 0;
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
};

///exception is thrown when cycle is detected during subscribtion
class CycleDetectedException : public std::exception {
public:
    virtual const char *what() const noexcept override {return "zerobus: Cycle detected";}
};

struct ChannelFilter {
    using ChannelList = IBridgeAPI::ChannelList;
    std::vector<std::pair<std::string, bool> > _whitelist;
    std::vector<std::pair<std::string, bool> > _blacklist;
    bool check(ChannelID id) const;
    ChannelList filter(ChannelList lst) const;
};

///Abstract bridge class. Extend this class to implement the bridge
class AbstractBridge: public IListener {
public:

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
    void apply_their_channels(ChannelList lst);

    ///Forward message from other side to connected broker
    /**
     * @param msg message to forward
     *
     * @note @b mt-safety: this method is mt-safe
     */
    void dispatch_message(Message &msg);
    void dispatch_message(Message &&msg);

    ///Call this function if peer has been reset
    /**
     * If peer is reset, it is expected, that peer unsubscribed all channels, so we must resend current
     * list. This function assumes, that no channels are subscribed on peer and calls send_mine_channels()
     *
     * @note @b mt-safety: this method is not mt-safe
     */
    void peer_reset();

    void register_monitor(IMonitor *mon) {
        _ptr->register_monitor(mon);
    }
    void unregister_monitor(const IMonitor *mon) {
        _ptr->unregister_monitor(mon);
    }

    void set_filter(ChannelFilter flt);


protected:
    ///overide - send channels to other side
    virtual void send_channels(const ChannelList &channels) noexcept = 0;
    ///overide - send message to other side
    virtual void send_message(const Message &msg) noexcept = 0;

protected:

    std::shared_ptr<IBridgeAPI> _ptr;

    std::vector<char> _char_buffer = {};
    std::vector<ChannelID> _cur_channels = {};
    std::size_t _chan_hash = 0;
    ChannelFilter _filter;
    bool _cycle_detected = false;

    static std::size_t hash_of_channel_list(const ChannelList &list);
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
