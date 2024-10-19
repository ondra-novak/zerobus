#pragma once
#include "bridge_api.h"

#include "filter.h"

#include <span>
#include <vector>
#include <atomic>
#include <functional>
namespace zerobus {


namespace Msg {

using Operation = IBridgeAPI::Operation;


using ChannelList = IBridgeAPI::ChannelList;
struct ChannelUpdate {
    ChannelList lst;
    Operation op;
    friend std::ostream &operator<<(std::ostream &out, const ChannelUpdate &ch) {
        out << "Update: " <<static_cast<int>(ch.op);
        char sep = '-';
        for (auto x: ch.lst) {
            out << sep << x;
            sep = ',';
        }
        return out;
    }
};

struct UpdateSerial {
    ChannelID serial;
    friend std::ostream &operator<<(std::ostream &out, const UpdateSerial &ch) {
        out << "Update serial:" << ch.serial;
        return out;
    }
};


struct ChannelReset {
    friend std::ostream &operator<<(std::ostream &out, const ChannelReset &) {
        out << "Chan Reset";
        return out;
    }
};
struct NewSession {
    unsigned long version = 1.0;
    friend std::ostream &operator<<(std::ostream &out, const NewSession &s) {
        out << "New session ver:" << s.version ;
        return out;
    }
};
struct NoRoute {
    ChannelID sender;
    ChannelID receiver;
    friend std::ostream &operator<<(std::ostream &out, const NoRoute &ch) {
        out << "No route:" << ch.sender << "," << ch.receiver;
        return out;
    }
};
struct CloseGroup {
    ChannelID group;
    friend std::ostream &operator<<(std::ostream &out, const CloseGroup &ch) {
        out << "Close group:" << ch.group;
        return out;
    }
};

struct GroupEmpty {
    ChannelID group;
    friend std::ostream &operator<<(std::ostream &out, const GroupEmpty &ch) {
        out << "Group Empty:" << ch.group;
        return out;
    }
};

struct AddToGroup {
    ChannelID group;
    ChannelID target;
    friend std::ostream &operator<<(std::ostream &out, const AddToGroup &ch) {
        out << "Add to group:" << ch.group << "," << ch.target;
        return out;
    }
};


}

///Abstract bridge class. Extend this class to implement the bridge
class AbstractBridge: public IListener {
public:


    using Operation = Msg::Operation;
    using ChannelList = Msg::ChannelList;
    using ChannelUpdate = Msg::ChannelUpdate;
    using ChannelReset =  Msg::ChannelReset;
    using NoRoute = Msg::NoRoute;
    using CloseGroup = Msg::CloseGroup;
    using AddToGroup = Msg::AddToGroup;
    using GroupEmpty = Msg::GroupEmpty;
    using NewSession = Msg::NewSession;
    using UpdateSerial = Msg::UpdateSerial;

    AbstractBridge(Bus bus);

    virtual ~AbstractBridge();


    AbstractBridge(const AbstractBridge &other):_ptr(other._ptr) {}
    AbstractBridge &operator=(const AbstractBridge &other) = delete;

    auto get_handle() const {return _ptr;}

    ///Sends list of channels of current broker to the other side
    /**
     * @param reset set true to enforce sending whole list ("replace");
     *
     * Retrieves active list of channels from a connected broker and generates a list which is then
     * forwarded to the function on_channels_update(). It also detects changes in the list and skips
     * sending the list if no change detected
     *
     * @note @b mt-safety: this method is mt-safe.
     */
    void send_mine_channels(bool reset = false) noexcept;

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
    void receive(const ChannelUpdate &chan_up);

    void receive(ChannelReset);


    ///apply their clear path command
    void receive(const NoRoute &cp);
    void receive(const UpdateSerial &msg);

    void receive(const CloseGroup &msg) ;
    void receive(const AddToGroup &msg);

    void receive(const Message &msg);
    void receive(const GroupEmpty &msg);
    void receive(const NewSession &msg);



    void register_monitor(IMonitor *mon) {
        _ptr->register_monitor(mon);
    }
    void unregister_monitor(const IMonitor *mon) {
        _ptr->unregister_monitor(mon);
    }

    ///set filter object
    /**
     * @param flt new filter object. Function stores previous filter object to the flt variable.
     *
     * @note this function is MT Safe when filter is set for the first time. If filter is removed
     * or replace, you should somehow ensure, that filter is not used after removal.
     */
    void set_filter(std::unique_ptr<Filter> &flt);

    Bus get_bus() const {return Bus(_ptr);}

    virtual void on_close_group(zerobus::ChannelID group_name) noexcept
            override;
    virtual void on_no_route(zerobus::ChannelID sender,
            zerobus::ChannelID receiver) noexcept override;
    virtual void on_add_to_group(zerobus::ChannelID group_name,
            zerobus::ChannelID target_id) noexcept override;
    virtual void on_group_empty(ChannelID group_name) noexcept override;


    bool is_disabled_for_cycle() const {return _cycle_detected;}


    static void install_cycle_detection_report(std::function<void(AbstractBridge *lsn, bool cycle)> rpt);

protected:
    ///override - send channels to other side
    /**
     * @param channels list channels
     * @param op operation with channels
     */
    virtual void send(const ChannelUpdate &msg) noexcept = 0;
    virtual void send(const Message &msg) noexcept = 0;
    virtual void send(const ChannelReset &) noexcept = 0;
    virtual void send(const CloseGroup &) noexcept = 0;
    virtual void send(const AddToGroup &) noexcept = 0;
    virtual void send(const NoRoute &) noexcept = 0;
    virtual void send(const GroupEmpty &) noexcept = 0;
    virtual void send(const NewSession &) noexcept = 0;
    virtual void send(const UpdateSerial &) noexcept = 0;


    ///diagnostic override called when cycle detection state changed;
    virtual void cycle_detection(bool ) noexcept;

    unsigned int get_version() const {return _version;}
protected:

    std::shared_ptr<IBridgeAPI> _ptr;

    std::vector<char> _char_buffer = {};    ///buffer to store character data for persistent channels
    std::vector<ChannelID> _cur_channels = {};
    std::vector<ChannelID> _tmp = {};   ///< temporary buffer for channel operations
    IBus::ChannelListStorage _bus_channels = {}; ///<temporary buffer to retrieve channels
    std::atomic<Filter *> _filter = {};
    std::atomic<unsigned int> _send_mine_channels_lock = {0};
    bool _cycle_detected = false;
    std::size_t _srl_hash = 0;
    unsigned int _version = 0;

    static ChannelList persist_channel_list(const ChannelList &source, std::vector<ChannelID> &channels, std::vector<char> &characters);

    virtual void on_message(const Message &message, bool pm) noexcept override;

    void process_mine_channels(ChannelList lst, bool reset) noexcept;


    void check_rules(Filter *flt);

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
