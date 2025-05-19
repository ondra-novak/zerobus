#pragma once

#include "bus.hpp"
#include "utils/channel.hpp"
#include "utils/routing_cache.h"
#include <unordered_map>
#include <queue>
#include <atomic>
#include <shared_mutex>



namespace zerobus {


class LocalBus : public IBus {
public:

    LocalBus();

    virtual bool subscribe(IListener *listener, ChannelID channel) override;
    virtual bool subscribe(IListener *listener, ChannelList channel)override;
    virtual void unsubscribe(IListener *listener, ChannelID channel)override;
    virtual void unsubscribe(IListener *listener, ChannelList channel)override;
    virtual bool send_message(IListener *listener,ChannelID channel,
            MessageContent msg, ConversationID cid) override;
    virtual bool forward_message(IListener *sender, const Message &msg) override;
    virtual bool is_channel(ChannelID id) const override;
    virtual UpdateSerialStatus update_serial(IListener *lsn, const SerialID &serialId) override;
    virtual void clear_path(ChannelID sender, ChannelID receiver, ConversationID cid) override;
    virtual ChannelList get_public_channels(
            IListener *listener, ChannelListStorage &storage) const override;
    virtual ChannelList get_subscribed_channels(IListener *listener,
            ChannelListStorage &storage) const override;
    virtual ChannelList get_subscribed_groups(
            IListener *listener, ChannelListStorage &storage) const override;
    virtual void close_private_channel(IListener *listener) override;
    virtual void unsubscribe_all(IListener *listener) override;
    virtual void close_group(IListener *owner, ChannelID group_name) override;
    virtual bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) override;
    virtual void channel_notify(IChannelNotifyListener *mon, bool enable) override;
    virtual SerialStatus get_serial() const override;
    virtual void close_all_groups(IListener *owner) override;
    virtual std::string get_random_channel_name(std::string_view prefix) const override;
    virtual void announce(IListener *listener, ConversationID req_id, ChannelID chan) override;


protected:


PublicChannelMap<IListener *> _public_channels;
PrivateChannelMap<IListener *> _private_channels;
utils::RoutingCache<IListener *> _routing_cache;
std::vector<IChannelNotifyListener *> _monitors;
mutable std::shared_mutex _mx;
std::atomic_flag _channels_no_change = {false};
std::string _node_serial = {};
SerialStatus _cur_serial = {};
mutable std::mutex _serial_mx;

void do_forward_message(Message &&msg, IListener *owner);


bool is_valid_target_lk(const ChannelID &chan, IListener *sender);
void notify_channel_change();

template<std::invocable<const Channel<IListener *> &>Pred>
ChannelList get_channels(ChannelListStorage &storage, Pred &&pred) const;
template<std::invocable<const Channel<IListener *> &> Pred>
void unsubscribe_helper(std::unique_lock<std::shared_mutex> &lk, Pred &&pred);

void do_forward_message(IListener *sender, const Message &msg) ;

template<typename ... Args>
void notify_monitors(void (IChannelNotifyListener::*fn)(Args ...), Args ... args);

private:
    std::string add_mailbox(zerobus::IListener *listener);
};
}

