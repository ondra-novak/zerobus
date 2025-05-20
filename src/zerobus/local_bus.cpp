#include "local_bus.hpp"
#include "utils/random_channel_gen.hpp"
#include "utils/recursive_dispatcher.hpp"
#include "utils/stack_alloc.hpp"

namespace zerobus {


using Dispatcher = utils::ThreadRecursiveDispatcher;

LocalBus::LocalBus():_node_serial(Bus::get_random_channel_name({})) {
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
            if (chan.empty()) result = false;
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
    utils::stack_alloc<PChannel>(channelList.size(),[&](PChannel *iter){
        std::lock_guard _(_mx);
        for (const auto &x: channelList) {
           auto c = _public_channels.find(x);
           if (c != _public_channels.end() && c->second->remove(listener)) {
               *iter++= std::move(c->second);
               _public_channels.erase(c);
               _channels_no_change.clear(std::memory_order_relaxed);
           }
        }
    });
    notify_channel_change();
}

bool LocalBus::is_valid_target_lk(const ChannelID &chan, IListener *sender) {
    return _private_channels.find(chan) != nullptr
            || _routing_cache.find_path(chan) != nullptr
            || _public_channels.find_channel_for_broadcast(chan, sender) != nullptr;
}

ChannelType LocalBus::get_channel_type(ChannelID id) const {
    std::shared_lock _(_mx);
    if (_private_channels.find(id) != nullptr) return ChannelType::private_channel;
    auto iter = _public_channels.find(id);
    if (iter != _public_channels.end()) {
        if (iter->second->get_owner()) return ChannelType::group;
        else return ChannelType::multicast_channel;
    }
    if (_routing_cache.find_path(id)) return ChannelType::private_channel;
    return ChannelType::not_used;
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

    std::string id;
    if (channel.empty()) return false;

    Dispatcher &disp = Dispatcher::get_instance();
    //acquire lock
    std::shared_lock lk(_mx); //_mx is recursive
    //test whether channel is valid target - reject if not
    if (!is_valid_target_lk(channel, listener)) return false;
    //search for sender
    std::string_view sender = _private_channels.find(listener);
    //sender is not created yet
    if (sender.empty()) {
        //we need temporary unlock this lock
        lk.unlock();
        //and finish any currently pending operation (to release locks)
        disp.finish();
        //add new mailbox (exclusive lock)
        sender = id = add_mailbox(listener);;
        //reacquire the lock
        lk.lock();
    }

    lk.unlock();
    //continue by forwarding message
    do_forward_message(listener, {sender, channel, content, cid});
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
    notify_monitors(&IChannelNotifyListener::on_announce, listener, req_id, std::string_view(c));
}

void LocalBus::do_forward_message(IListener *sender, const Message &msg) {
    //contains pointer selected channel
    MyChannel *c = nullptr;
    //contains pointer to broadcasting position variable
    std::size_t *pos = nullptr;
    //dispatcher
    Dispatcher &disp = Dispatcher::get_instance();
    //this is true, if we are dispatching
    bool indisp = disp.is_dispatching();
    //if we are dispatching, message is allocated on heap, otherwise it is on stack
    auto msg_deleter = [indisp](const Message *msg) {
        if (indisp) {
            msg->~Message();
            ::operator delete(const_cast<Message *>(msg));
        }
    };
    //construct special unique pointer
    using MPtr = std::unique_ptr<const Message, decltype(msg_deleter)>;
    //this pointer releases memory when message is on heap,
    //but not when it is on stack
    MPtr msg_ptr(nullptr, msg_deleter);

    //if we dispatching, copy message to heap
    if (indisp) {
        //determine how many bytes we need
        std::size_t needsz = sizeof(Message)+msg.size_bytes();
        //allocate them
        void *b = ::operator new(needsz);
        //retrieve pointer to data buffer
        auto iter = static_cast<char *>(b)+sizeof(Message);
        //construct copy with data
        Message *mcpy = new(b) Message(msg.copy(iter));
        //store pointer
        msg_ptr.reset(mcpy);
    } else {
        //store pointer to stack
        msg_ptr.reset(&msg);
    }

    //during the first call, we copy the pointer
    //to the space of the callback's stack.
    //This pointer is later initialized to point on the variable
    //this protects message to not be destroyed even if
    //the disp.finish() is callled - so the handler can use it futhrer
    MPtr *mptr_lnk = nullptr;

    //construct dispatch task
    disp.enqueue([this, c, pos, sender, mptr_lnk,
                  mptr = std::move(msg_ptr),
                  lk = std::shared_lock(_mx, std::defer_lock)]()mutable{

        //if we already broadcasting
        if (c) {
            //finish broadcasting
            c->broadcast(sender, *(*mptr_lnk), *pos);
        //test first call
        } else if (!lk.owns_lock()){
            //copy pointer to stack
            auto msg_ptr = std::move(mptr);
            //link this pointer
            mptr_lnk = &msg_ptr;
            //retrieve channel
            auto chan = msg_ptr->get_channel();
            //lock the mutex (shared)
            lk.lock();
            //is it private channel?
            auto trg = _private_channels.find(chan);
            if (trg) {
                //send as private message
                trg->on_direct_message(*msg_ptr);
                return;
            }
            //is it channel or group?
            c = _public_channels.find_channel_for_broadcast(chan, sender);
            if (c) {
                //create broadcast status
                std::size_t stpos = 0;
                //set pointer to this status
                pos = &stpos;
                //perform broadcast
                c->broadcast(sender, *msg_ptr, *pos);
                return;
            }
            //target is external?
            trg = _routing_cache.find_path(chan);
            if (trg) {
                //otherwise forward the message to the bridge
                trg->on_message(*msg_ptr);
                return;
            }
            //message cannot be delivered - send it back
            trg = _private_channels.find(msg_ptr->sender);
            //if there is such targe
            if (trg) {
                trg->on_no_route(msg_ptr->sender, chan, msg_ptr->cid);
                return;
            }
            //discard message
        }
    });
    //start dispatching if needed
    if (!indisp) disp.dispatch();
}



bool LocalBus::is_channel(ChannelID id) const {
    std::shared_lock lk(_mx);
    auto c = _public_channels.find_channel_for_broadcast(id, nullptr);
    return static_cast<bool>(c);
}

void LocalBus::clear_path(ChannelID sender, ChannelID receiver, ConversationID cid) {
    Dispatcher &disp = Dispatcher::get_instance();
    disp.finish();    //finish pending, unlock all locks
    std::unique_lock lk(_mx);

    IListener *lsn = _private_channels.find(sender);
    if (!lsn) lsn = _routing_cache.find_path(sender);
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
    Dispatcher &disp=Dispatcher::get_instance();
    disp.finish();
    std::unique_lock lk(_mx);
    IListener *trg = _private_channels.find(uid);
    if (!trg) trg = _routing_cache.find_path(uid);
    if (!trg) return false;
    auto c = _public_channels.create_channel(group_name, owner);
    if (!c) return false;
    c->add(trg);
    disp.enqueue([&, lk = std::move(lk), once = false]()mutable{
        if (once) {
            return;
        }
        once = true;
        trg->on_add_to_group(group_name, uid);
    });
    disp.dispatch();
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

void LocalBus::set_ttl(std::chrono::seconds timeout) {
    _routing_cache.set_ttl(timeout);
}

}
