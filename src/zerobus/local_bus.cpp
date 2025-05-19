#include "local_bus.hpp"
#include "utils/random_channel_gen.hpp"
#include "utils/recursive_dispatcher.hpp"
#include "utils/stack_alloc.hpp"

namespace zerobus {


using Dispatcher = utils::ThreadRecursiveDispatcher;

LocalBus::LocalBus():_node_serial(LocalBus::get_random_channel_name({})) {
_cur_serial.serial = _node_serial;
}

template<typename ... Args>
void LocalBus::notify_monitors(void (IChannelNotifyListener::*fn)(Args ...), Args ... args) {
    Dispatcher &disp = Dispatcher::get_instance();
    std::size_t *pos = nullptr;
    disp.enqueue([this, pos, lk = std::shared_lock(_mx, std::defer_lock), fn, args...]() mutable{
        std::size_t stpos = 0;
        if (!lk.owns_lock()) {
            if (_channels_no_change.test_and_set(std::memory_order_relaxed)) return;
            lk.lock();
        }
        //create storage for position
        if (!pos) pos = &stpos;
        std::size_t &posr = *pos;   //'pos' may be unavailable eventually

        std::size_t cnt = _monitors.size();
        while (posr < cnt) {
            IChannelNotifyListener *p = _monitors[posr];
            ++posr;
            (p->*fn)(args...);
        }
    });
    disp.dispatch_if_needed();
}



void LocalBus::notify_channel_change() {
    notify_monitors(&IChannelNotifyListener::on_channels_update);
}


bool LocalBus::subscribe(IListener *listener, ChannelList channelList){
    Dispatcher::get_instance().finish(); //finish any pending action
    bool result = true;
    {
        std::lock_guard _(_mx);
        for (const auto &chan: channelList) {
            auto c = _public_channels.create_channel(chan, nullptr);
            if (c == nullptr) result = false;
            else {
                c->add(listener);
                _channels_no_change.clear(std::memory_order_relaxed);
            }
        }
    }
    notify_channel_change();
    return result;
}
void LocalBus::unsubscribe(IListener *listener, ChannelList channelList){
    Dispatcher::get_instance().finish(); //finish any pending action
    {
        std::lock_guard _(_mx);
        for (const auto &x: channelList) {
           auto c = _public_channels.find_channel_for_broadcast(x, nullptr);
           if (c && c->remove(listener)) {
               _public_channels.erase(x);
               _channels_no_change.clear(std::memory_order_relaxed);
           }
        }
    }
    notify_channel_change();
}




bool LocalBus::is_valid_target_lk(const ChannelID &chan, IListener *sender) {
    return _private_channels.find(chan) != nullptr
            || _routing_cache.find_path(chan) != nullptr
            || _public_channels.find_channel_for_broadcast(chan, sender) != nullptr;
}

std::string LocalBus::add_mailbox(zerobus::IListener *listener) {
    std::string id("@");
    //sender has no mailbox, it must be created, switch to exclusive lock
    std::unique_lock _(_mx);
    generate_mailbox_id(std::back_inserter(id));
    _private_channels.add(id, listener);
    return id;
}

bool LocalBus::send_message(zerobus::IListener *listener,
        zerobus::ChannelID channel, zerobus::MessageContent content,
        zerobus::ConversationID cid) {

    //finish any pending task now (may held locks)
    Dispatcher::get_instance().finish();
    //we need just shared lock
    std::shared_lock lk(_mx);

    if (!is_valid_target_lk(channel, listener)) return false;

    std::string_view sender = _private_channels.find(listener);
    lk.unlock();
    std::string id;
    if (listener && sender.empty()) {
        sender = id = add_mailbox(listener);
    }
    do_forward_message(nullptr, {sender, channel, content, cid});
    return true;

}


bool LocalBus::forward_message(IListener *sender, const Message &msg) {
    Dispatcher::get_instance().finish();
    {
        std::lock_guard _(_mx);
        if (!is_valid_target_lk(msg.get_channel(), sender)) return false;
        _routing_cache.register_path(msg.get_sender(), sender);
    }
    do_forward_message(sender, msg);
    return true;

}

void LocalBus::announce(IListener *listener, ConversationID req_id, ChannelID chan) {
    Dispatcher &disp = Dispatcher::get_instance();
    disp.finish();
    std::string c;
    if (!chan.empty())  {
        std::lock_guard _(_mx);
        if (!_routing_cache.register_path(chan, listener, req_id)) return;
        c = chan;
    } else {
        std::lock_guard _(_mx);
        chan = _private_channels.find(listener);
        if (chan.empty()) {
            c = add_mailbox(listener);
        } else {
            c = chan;
        }
    }
    notify_monitors(&IChannelNotifyListener::on_announce, req_id, std::string_view(c));
}

void LocalBus::do_forward_message(IListener *sender, const Message &msg) {
    Channel<IListener *> *c = nullptr;
    std::size_t *pos = nullptr;
    Dispatcher &disp = Dispatcher::get_instance();
    bool indisp = disp.is_dispatching();
    auto msg_deleter = [indisp](const Message *msg) {
        if (indisp) delete msg;
    };
    using MPtr = std::unique_ptr<const Message, decltype(msg_deleter)>;
    MPtr msg_ptr(nullptr, msg_deleter);

    if (indisp) {
        auto channel = msg.get_channel();
        auto content = msg.get_content();
        auto sender = msg.get_sender();
        std::size_t needsz = sizeof(Message)+channel.size()+content.size()+sender.size();
        void *b = ::operator new(needsz);
        Message *mcpy = static_cast<Message *>(b);
        auto iter = static_cast<char *>(b)+sizeof(Message);
        auto cpy_str = [&iter](auto &cont) {
            auto end = std::copy(cont.begin(), cont.end(), iter);
            cont = {iter, end};
            iter = end;
        };
        cpy_str(sender);cpy_str(channel);cpy_str(content);
        std::construct_at(mcpy, sender, channel, content, msg.get_conversation());
        msg_ptr.reset(mcpy);
    } else {
        msg_ptr.reset(&msg);
    }

    MPtr *mptr_lnk = nullptr;


    disp.enqueue([this, c, pos, sender, mptr = std::move(msg_ptr), mptr_lnk,
                  lk = std::shared_lock(_mx, std::defer_lock)]()mutable{

        if (c) {
            c->broadcast(sender, *(*mptr_lnk), *pos);
        } else if (!lk.owns_lock()){
            bool pm = true;
            auto msg_ptr = std::move(mptr);
            mptr_lnk = &msg_ptr;
            auto chan = msg_ptr->get_channel();
            lk.lock();
            auto trg = _private_channels.find(chan);
            if (!trg) {
                trg = _routing_cache.find_path(chan);
                pm = false;
            }
            if (!trg) {
                std::size_t stpos = 0;  //create space on broadcast status
                pos = &stpos;
                c = _public_channels.find_channel_for_broadcast(chan, sender);
                if (c) c->broadcast(sender, *msg_ptr, *pos);
            } else {
                trg->on_message(*msg_ptr, pm);
            }
        }
    });
    disp.dispatch_if_needed();



}



bool LocalBus::is_channel(ChannelID id) const {
    Dispatcher::get_instance().finish();
    std::shared_lock lk(_mx);
    auto c = _public_channels.find_channel_for_broadcast(id, nullptr);
    return static_cast<bool>(c);
}

void LocalBus::clear_path(ChannelID sender, ChannelID receiver, ConversationID cid) {
    Dispatcher &disp = Dispatcher::get_instance();
    disp.finish();    //finish pending, unlock all locks
    std::unique_lock lk(_mx);
    IListener *lsn = _routing_cache.find_path(sender);
    _routing_cache.clear_path(receiver);
    if (lsn) {
        disp.enqueue([lsn, &sender, &receiver, cid, lk = std::move(lk)]() mutable{
            if (!lsn) return;
            auto l = lsn;
            lsn = nullptr;
            l->on_no_route(sender, receiver,cid);
        });
        disp.dispatch();
    }
}

template<std::invocable<const Channel<IListener *> &> Pred>
ChannelList LocalBus::get_channels(ChannelListStorage &storage, Pred &&pred) const{
    Dispatcher::get_instance().finish();
    std::shared_lock lk(_mx);
    std::size_t need_cnt = 0;
    for (const auto &[chan, ptr]: _public_channels) {
        if (!pred(*ptr)) continue;
        ++need_cnt;
    }
    return utils::stack_alloc<ChannelID>(need_cnt, [&](ChannelID *lst){
        std::size_t pos = 0;
        for (const auto &[chan, ptr]: _public_channels) {
            if (!pred(*ptr)) continue;
            lst[pos] = ChannelID(ptr->get_name());
            ++pos;
        }
        return storage.store_channels(ChannelList(lst, need_cnt));
    });
}

ChannelList LocalBus::get_subscribed_channels(IListener *listener,
        ChannelListStorage &storage) const {
    return get_channels(storage, [&](const Channel<IListener *> &chan){
       return chan.get_owner() == nullptr && chan.contains(listener);
    });
}
ChannelList LocalBus::get_subscribed_groups(IListener *listener,
        ChannelListStorage &storage) const {
    return get_channels(storage, [&](const Channel<IListener *> &chan){
       return chan.get_owner() != nullptr && chan.contains(listener);
    });
}
ChannelList LocalBus::get_public_channels(IListener *listener,
        ChannelListStorage &storage) const {
    return get_channels(storage, [&](const Channel<IListener *> &chan){
       return chan.get_owner() == nullptr &&
        (chan.size()>1 || !chan.contains(listener));
    });
}

void LocalBus::close_private_channel(IListener *listener) {
    Dispatcher::get_instance().finish();
    std::lock_guard _(_mx);
    _private_channels.erase(listener);

}

void LocalBus::unsubscribe_all(IListener *listener) {
    Dispatcher::get_instance().finish();
    std::unique_lock<std::shared_mutex> lk(_mx);
    _private_channels.erase(listener);
    _routing_cache.clear_bridge(listener);
    if (_cur_serial.source == listener) {
        _cur_serial.serial = _node_serial;
        _cur_serial.source = nullptr;
        _channels_no_change.clear(std::memory_order_relaxed);
    }
    unsubscribe_helper(lk, [&](auto &chan) {
        if constexpr(std::is_const_v<std::remove_reference_t<decltype(chan)> >) {
            return chan.get_owner() == listener || (chan.size() == 1 && chan.contains(listener));
        } else {
            return chan.get_owner() == listener || chan.remove(listener);
        }
    });
}

template<std::invocable<const Channel<IListener *> &> Pred>
void LocalBus::unsubscribe_helper(std::unique_lock<std::shared_mutex> &lk, Pred &&pred) {
    std::size_t needsz = 0;
    auto iter = _public_channels.begin();
    while (iter != _public_channels.end()) {
        if (pred(const_cast<const Channel<IListener *> &>(*iter->second))) ++needsz;
        ++iter;
    }
    utils::stack_alloc<std::unique_ptr<Channel<IListener *> > >(needsz, [&](auto *ptr){
        auto to_destroy = ptr;
        auto iter = _public_channels.begin();
        while (iter != _public_channels.end()) {
            if (pred(*iter->second)) {
                *to_destroy = std::move(iter->second);
                ++to_destroy;
                iter = _public_channels.erase(iter);
            } else {
                ++iter;
            }
        }
        lk.unlock();
        if (to_destroy != ptr) {
            _channels_no_change.clear(std::memory_order_relaxed);
        }
        //desructor of array deletes channels outside of lock
    });
    notify_channel_change();
}

void LocalBus::close_group(IListener *owner, ChannelID group_name) {
    Dispatcher::get_instance().dispatch();
    std::unique_ptr<Channel<IListener *> > c;
    {
        std::lock_guard lk(_mx);
        auto iter = _public_channels.find(group_name);
        if (iter == _public_channels.end()
                || iter->second->get_owner() != owner) return;
        c = std::move(iter->second);
        _public_channels.erase(iter);
    }
    //destructor of c deletes channel

}

bool LocalBus::add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) {
    Dispatcher::get_instance().dispatch();
    std::lock_guard lk(_mx);
    IListener *trg = _private_channels.find(uid);
    if (!trg) trg = _routing_cache.find_path(uid);
    if (!trg) return false;
    auto c = _public_channels.create_channel(group_name, owner);
    if (!c) return false;
    c->add(trg);
    return true;
}

void LocalBus::channel_notify(IChannelNotifyListener *mon, bool enable) {
    Dispatcher::get_instance().dispatch();
    std::lock_guard lk(_mx);
    _monitors.erase(std::remove(_monitors.begin(), _monitors.end(), mon), _monitors.end());
    if (enable) {
        _monitors.push_back(mon);
    }
}

 void LocalBus::close_all_groups(IListener *owner) {
     Dispatcher::get_instance().dispatch();
     std::unique_lock<std::shared_mutex> lk(_mx);
     unsubscribe_helper(lk, [&](const Channel<IListener *> &chan) {
         return chan.get_owner() == owner;
     });
 }

 std::string LocalBus::get_random_channel_name(std::string_view prefix) const {
     std::string id(prefix);
     generate_mailbox_id(std::back_inserter(id));
     return id;

 }

 UpdateSerialStatus LocalBus::update_serial(IListener *lsn, const SerialID &serialId) {
    std::unique_lock lk(_serial_mx);
    UpdateSerialStatus st = UpdateSerialStatus::not_changed;
    if (_cur_serial.serial == serialId) {
        st = _cur_serial.source != lsn?UpdateSerialStatus::cycle:UpdateSerialStatus::same;
    } else if (_cur_serial.serial < serialId) {
        _cur_serial.serial.clear();
        _cur_serial.serial.append(serialId);
        _cur_serial.source = lsn;
        st = UpdateSerialStatus::changed;
    } else if (_cur_serial.source == lsn && lsn) {
        _cur_serial.serial = serialId;
        st = UpdateSerialStatus::changed;
    }
    if (st == UpdateSerialStatus::changed) {
        _channels_no_change.clear(std::memory_order_relaxed);
        lk.unlock();
        notify_channel_change();
    }
    return st;
}

SerialStatus LocalBus::get_serial() const {
    std::unique_lock _(_serial_mx);
    return _cur_serial;
}

Bus Bus::create() {
    return Bus(std::make_shared<LocalBus>());
}

}
