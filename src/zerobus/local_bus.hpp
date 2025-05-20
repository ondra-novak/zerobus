#pragma once

#include "bus.hpp"
#include "utils/channel.hpp"
#include "utils/routing_cache.h"
#include "utils/recursive_shared_lock.hpp"
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
            MessageContent msg, ConversationID cid);
    bool forward_message(IListener *sender, const Message &msg);
    bool is_channel(ChannelID id) const;
    UpdateSerialStatus update_serial(IListener *lsn, const SerialID &serialId);
    void clear_path(ChannelID sender, ChannelID receiver, ConversationID cid);
    ChannelList get_public_channels(IListener *listener,
            ChannelListStorage &storage) const;
    ChannelList get_subscribed_channels(IListener *listener,
            ChannelListStorage &storage) const;
    ChannelList get_subscribed_groups(IListener *listener,
            ChannelListStorage &storage) const;
    void close_private_channel(IListener *listener);
    void unsubscribe_all(IListener *listener);
    void close_group(IListener *owner, ChannelID group_name);
    bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid);
    void channel_notify(IChannelNotifyListener *mon, bool enable);
    SerialStatus get_serial() const;
    void close_all_groups(IListener *owner);
    void announce(IListener *listener, ConversationID req_id, ChannelID chan);
    ChannelType get_channel_type(ChannelID id) const;
    void set_ttl(std::chrono::seconds timeout);

protected:

    using MyChannel = Channel<IListener *>;
    using PChannel = std::unique_ptr<MyChannel>;


    PublicChannelMap<IListener*> _public_channels;
    PrivateChannelMap<IListener*> _private_channels;
    utils::RoutingCache<IListener*> _routing_cache;
    std::vector<IChannelNotifyListener*> _monitors;
    mutable recursive_shared_mutex _mx;
    std::atomic_flag _channels_no_change = { false };
    std::string _node_serial = { };
    SerialStatus _cur_serial = { };
    mutable std::mutex _serial_mx;

    void do_forward_message(Message &&msg, IListener *owner);

    bool is_valid_target_lk(const ChannelID &chan, IListener *sender);
    void notify_channel_change();

    template<std::invocable<const Channel<IListener*>&> Pred>
    ChannelList get_channels(ChannelListStorage &storage, Pred &&pred) const;
    template<std::invocable<const Channel<IListener*>&> Pred>
    void unsubscribe_helper(std::unique_lock<std::shared_mutex> &lk,
            Pred &&pred);

    void do_forward_message(IListener *sender, const Message &msg);

    template<typename ... Args>
    void notify_monitors(void (IChannelNotifyListener::*fn)(Args ...),
            Args ... args);

private:
    std::string add_mailbox(zerobus::IListener *listener);
};
}

