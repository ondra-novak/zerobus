#pragma once

#include "bus.hpp"
#include "utils/channel.hpp"
#include "utils/routing_cache.h"
#include "utils/callable_variant.hpp"
#include "utils/recursive_shared_lock.hpp"
#include "utils/hybrid_unique_ptr.hpp"
#include "utils/recursive_dispatcher.hpp"
#include <unordered_map>
#include <queue>
#include <atomic>
#include <shared_mutex>

namespace zerobus {

class LocalBus {
public:

    LocalBus();

    bool subscribe(IListener *listener, ChannelList channel);
    void unsubscribe(IListener *listener, ChannelList channel);
    bool send_message(IListener *listener, ChannelID channel,
            MessageContent msg, ConversationID cid, MsgFlags imptc);
    bool forward_message(IListener *sender, const Message &msg);
    bool is_group(IListener *owner, ChannelID id) const;
    UpdateSerialStatus update_serial(IListener *lsn, const SerialID &serialId);
    void delivery_error(const Undelivered &msg);
    ChannelList get_public_channels(IListener *listener,
            ChannelListStorage &storage) const;
    ChannelList get_subscribed_channels(IListener *listener,
            ChannelListStorage &storage) const;
    ChannelList get_subscribed_groups(IListener *listener,
            ChannelListStorage &storage) const;
    ChannelID create_private_channel(IListener *listener);
    void close_private_channel(IListener *listener);
    void unsubscribe_all(IListener *listener);
    void close_group(IListener *owner, ChannelID group_name);
    bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid, ConversationID cid);
    void channel_notify(IChannelNotifyListener *mon, bool enable);
    SerialStatus get_serial() const;
    void close_all_groups(IListener *owner);
    void announce(IListener *listener, ConversationID req_id, ChannelID chan);
    ChannelType get_channel_type(ChannelID id) const;
    void defer(FunctionView<void()> fn);
    void set_routing_ttl(std::chrono::system_clock::duration ttl);
protected:

    using MyChannel = Channel<IListener *>;
    using PChannel = std::unique_ptr<MyChannel>;



    template<typename Derived>
    class NotifyMonitorsBaseQI {
    public:
        NotifyMonitorsBaseQI(LocalBus *owner);
        void operator()();

    protected:
        LocalBus *_owner;
        std::size_t *pos = nullptr;
        std::shared_lock<recursive_shared_mutex> _lk;
    };

    class NotifyChannelUpdateQI: public NotifyMonitorsBaseQI<NotifyChannelUpdateQI> {
    public:
        NotifyChannelUpdateQI(LocalBus *owner);
        void run(IChannelNotifyListener *p);
    };

    class NotifyAnounceQI: public NotifyMonitorsBaseQI<NotifyAnounceQI> {
    public:
        NotifyAnounceQI(LocalBus *owner,IListener *sender, ConversationID reqid, std::string chan);
        void run(IChannelNotifyListener *p);
    protected:
        IListener *_sender;
        ConversationID _reqid;
        std::string _chan;
    };


    //state held during broadcasting
    struct ForwardState {
        //channel involved in broadcasting - nullptr if none
        MyChannel *_channel;
        //position in channel
        std::size_t _pos;
        //pointer to message
        HybridUniquePtr<const Message> _mptr;
        //listener to be removed from the target channel on exit
        IListener *_remove = nullptr;

    };

    class ForwardMsgQI {
    public:
        ForwardMsgQI(LocalBus *owner, IListener *sender, HybridUniquePtr<const Message> mptr);
        void operator()();
    protected:

        //pointer to owning instance
        LocalBus *_owner;
        //send of the message
        IListener *_sender;
        //message itself
        HybridUniquePtr<const Message> _mptr;
        //pointer to state (only valid on enter)
        ForwardState *_state = nullptr;
        //shared lock guard
        std::shared_lock<recursive_shared_mutex> _lk;
    };

    class DeliveryErrorQI {
    public:
        DeliveryErrorQI(IListener *lsn, const Undelivered &msg,
                    std::unique_lock<recursive_shared_mutex> lk);
        void operator()();
    protected:
        IListener *_lsn;
        const Undelivered &_msg;
        std::unique_lock<recursive_shared_mutex> _lk;

    };

    class AddToGroupQI {
    public:
        AddToGroupQI(IListener *trg, const ChannelID &group_name,
                const ChannelID &uid,  ConversationID cid,
                std::unique_lock<recursive_shared_mutex> lk);
        void operator()();
    protected:
        IListener *_trg;
        const ChannelID &_group_name;
        const ChannelID &_uid;
        ConversationID _cid;
        std::unique_lock<recursive_shared_mutex> _lk;
        bool _once = false;

    };


    using DispMsgBase = CallableVariant<void(),
            NotifyChannelUpdateQI,
            NotifyAnounceQI,
            ForwardMsgQI,
            DeliveryErrorQI,
            AddToGroupQI>;

    //declare user function, calculate required space
    using UserFunction = Function<void(),
            //max occupied space
            MaxSizeOfCallableVarian<DispMsgBase>::value
                //substract extra cost for Function itself
                - sizeof(Function<void(),sizeof(void *)>) - sizeof(void *)>;

    //create new list which includes UserFunction
    using DispMsg = typename AddToCallableVariant<DispMsgBase, UserFunction>::type;


    PublicChannelMap<IListener*> _public_channels;
    PrivateChannelMap<IListener*> _private_channels;
    utils::RoutingCache<IListener*> _routing_cache;
    std::vector<IChannelNotifyListener*> _monitors;
    mutable recursive_shared_mutex _mx;
    std::atomic_flag _channels_no_change = { false };
    std::string _node_serial = { };
    SerialStatus _cur_serial = { };

    bool do_forward_message(Message &&msg, IListener *owner);

    bool is_valid_target_lk(const ChannelID &chan, IListener *sender);
    void notify_channel_change();

    template<std::invocable<const Channel<IListener*>&> Pred>
    ChannelList get_channels(ChannelListStorage &storage, Pred &&pred) const;
    template<std::invocable<const Channel<IListener*>&> Pred>
    void unsubscribe_helper(std::unique_lock<std::shared_mutex> &lk,
            Pred &&pred);

    void do_forward_message(IListener *sender, const Message &msg);

    std::string add_mailbox(zerobus::IListener *listener);

    static thread_local utils::RecursiveDispatcher<DispMsg> _disp;
};
}

