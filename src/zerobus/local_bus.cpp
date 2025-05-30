#include "listener.hpp"
#include "local_bus.hpp"
#include "undelivered.hpp"
#include "channel_notify_listener.hpp"
#include "channel_list_storage.hpp"
#include "utils/random_channel_gen.hpp"
#include "utils/stack_alloc.hpp"
namespace zerobus {


thread_local utils::RecursiveDispatcher<LocalBus::DispMsg> LocalBus::_disp = {};


LocalBus::LocalBus():_node_serial(Bus::get_random_channel_name({})) {
_cur_serial.serial = _node_serial;
}

template<typename Derived>
LocalBus::NotifyMonitorsBaseQI<Derived>::NotifyMonitorsBaseQI(LocalBus *owner)
    :_owner(owner),_lk(owner->_mx,std::defer_lock) {}

template<typename Derived>
void LocalBus::NotifyMonitorsBaseQI<Derived>::operator()() {
    std::size_t stpos = 0;
    if (!_lk.owns_lock()) {
        if (_owner->_channels_no_change.test_and_set(std::memory_order_relaxed)) return;
        _lk.lock();
    }
    //create storage for position
    if (!pos) pos = &stpos;
    std::size_t &posr = *pos;   //'pos' may be unavailable eventually

    std::size_t cnt = _owner->_monitors.size();
    while (posr < cnt) {
        IChannelNotifyListener *p = _owner->_monitors[posr];
        ++posr;
        static_cast<Derived *>(this)->run(p);
    }
}

LocalBus::NotifyChannelUpdateQI::NotifyChannelUpdateQI(LocalBus *owner)
    :NotifyMonitorsBaseQI<NotifyChannelUpdateQI>(owner) {}
void LocalBus::NotifyChannelUpdateQI::run(IChannelNotifyListener *p) {
    p->on_channels_update();
}

LocalBus::NotifyAnounceQI::NotifyAnounceQI(LocalBus *owner,IListener *sender, ConversationID reqid, std::string chan)
    :NotifyMonitorsBaseQI<NotifyAnounceQI>(owner)
    ,_sender(sender),_reqid(reqid),_chan(std::move(chan)) {}
void LocalBus::NotifyAnounceQI::run(IChannelNotifyListener *p) {
    p->on_announce(_sender, _reqid, _chan);
}

template<bool single_recv>
LocalBus::ForwardMsgQI<single_recv>::ForwardMsgQI(LocalBus *owner, IListener *sender, HybridUniquePtr<const Message> mptr)
    :_owner(owner)
    ,_sender(sender)
    ,_mptr(std::move(mptr))
    ,_lk(_owner->_mx, std::defer_lock) {}

template<bool single_recv>
void LocalBus::ForwardMsgQI<single_recv>::operator()() {
    //if we already broadcasting
    if (_state && _state->_channel) {
        if constexpr(!single_recv) {
            //finish broadcasting
            _state->_channel->broadcast(_sender, *_state->_mptr, _state->_pos);
        } else {
            //reset channel pointer
            //because pointer can be marked as owned,
            //it can cause release of the channel,
            //which can cause calling the callback on_close_group()
            //which can re-enter here
            //so on next re-enter the _state->_channel is nullptr;
            _state->_channel.reset();
        }
    //test first call
    } else if (!_lk.owns_lock()){

        ForwardState state{{nullptr, false}, 0, std::move(_mptr)};
        _state = &state;
        //retrieve channel
        auto chan = state._mptr->get_channel();
        //lock the mutex (shared)
        _lk.lock();
        //is it private channel?
        auto trg = _owner->_private_channels.find(chan);
        if (trg) {
            //send as private message
            trg->on_direct_message(*state._mptr);
            return;
        }

        //not for single receiver (flag)
        if constexpr(!single_recv) {
           //is it channel or group?
            state._channel.reset(_owner->_public_channels.find_channel_for_broadcast(chan, _sender));
            if (state._channel) {
                //perform broadcast
                state._channel->broadcast(_sender, *state._mptr, state._pos);
                return;
            }
        //is for single receiver (this should be locked exclusively)
        } else {
            //find channel
            auto iter = _owner->_public_channels.find(chan);
            //if found - continue onward
            if (iter != _owner->_public_channels.end()) {
                //pick owner
                auto chan_owner = iter->second->get_owner();
                //if channel can be used for broadcast
                if (chan_owner == nullptr || chan_owner == _sender) {
                    //pop listener
                    auto lsn = iter->second->pop();
                    //if public channel, or there isn't flag kickOutReceiver
                    if (chan_owner == nullptr
                       || !contains<MsgFlags::kickOutReceiver>(state._mptr->flags)) {
                        //push listener back
                        iter->second->push(lsn);
                    } else {
                        //otherwise check whether is empty
                        if (iter->second->empty()) {
                            //if empty, steal its pointer and
                            //store it for destruction at the end
                            state._channel = HybridUniquePtr<MyChannel>{
                                iter->second.release(), true
                            };
                            //remove empty channel
                            _owner->_public_channels.erase(iter);
                        }
                    }
                    //now deliver message (may be re-entrant
                    lsn->on_message(*state._mptr);
                    //we are done here
                    return;
                }
            }
        }
        //target is external?
        trg = _owner->_routing_cache.find_path(chan);
        if (trg) {
            //otherwise forward the message to the bridge
            trg->on_message(*state._mptr);
            return;
        }
        //message cannot be delivered - send it back
        trg = _owner->_private_channels.find(state._mptr->sender);
        //if there is such targe
        if (trg) {
            trg->on_delivery_error(Undelivered{
                state._mptr->sender, chan,
                state._mptr->cid, DeliveryError::no_route,
                state._mptr->flags
            });
            return;
        }
        //discard message
    }
}

LocalBus::DeliveryErrorQI::DeliveryErrorQI(IListener *lsn, const Undelivered &msg,
            std::unique_lock<recursive_shared_mutex> lk)
    :_lsn(lsn),_msg(msg),_lk(std::move(lk)) {}

void LocalBus::DeliveryErrorQI::operator()() {
    if (!_lsn) return;
    auto l = _lsn;
    _lsn = nullptr;
    l->on_delivery_error(_msg);
}

LocalBus::AddToGroupQI::AddToGroupQI(IListener *trg, const ChannelID &group_name,
        const ChannelID &uid,  ConversationID cid,
        std::unique_lock<recursive_shared_mutex> lk)
:_trg(trg),_group_name(group_name),_uid(uid),_cid(cid), _lk(std::move(lk)) {}
void LocalBus::AddToGroupQI::operator()() {
    if (_once) return;
    _once = true;
    _trg->on_add_to_group(_group_name, _uid, _cid);

}


void LocalBus::notify_channel_change() {
    _disp.enqueue(NotifyChannelUpdateQI(this));
    _disp.dispatch_if_needed();
}


bool LocalBus::subscribe(IListener *listener, ChannelList channelList){
    _disp.finish();
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
    _disp.finish();
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

std::string LocalBus::add_mailbox(IListener *listener) {
    std::string id("@");
    //sender has no mailbox, it must be created, switch to exclusive lock
    std::unique_lock _(_mx);
    generate_mailbox_id(std::back_inserter(id));
    _private_channels.add(id, listener);
    return id;
}

bool LocalBus::send_message(IListener *listener, ChannelID channel, MessageContent content,
        ConversationID cid, MsgFlags imptc) {

    std::string id;
    if (channel.empty()) return false;

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
        _disp.finish();
        //add new mailbox (exclusive lock)
        sender = id = add_mailbox(listener);;
        //reacquire the lock
        lk.lock();
    }

    lk.unlock();
    //continue by forwarding message
    do_forward_message(listener, {sender, channel, content, cid, imptc});
    return true;

}


bool LocalBus::forward_message(IListener *sender, const Message &msg) {
    _disp.finish();
    {
        std::lock_guard _(_mx);
        if (!is_valid_target_lk(msg.get_channel(), sender)) return false;
        _routing_cache.register_path(msg.get_sender(), sender);
    }
    do_forward_message(sender, msg);
    return true;

}

void LocalBus::announce(IListener *listener, ConversationID req_id, ChannelID chan) {
    _disp.finish();
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
    _disp.enqueue(NotifyAnounceQI(this,listener, req_id, std::move(c)));
    _disp.dispatch_if_needed();
}

void LocalBus::do_forward_message(IListener *sender, const Message &msg) {
    //this is true, if we are dispatching
    bool indisp = _disp.is_dispatching();
    //construct special unique pointer
    using MPtr = HybridUniquePtr<const Message>;
    //this pointer releases memory when message is on heap,
    //but not when it is on stack
    MPtr msg_ptr(nullptr, indisp);

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

    if (contains<MsgFlags::singleReceiver>(msg_ptr->flags)) {
        _disp.enqueue(ForwardMsgQI<true>(this, sender,std::move(msg_ptr)));
    } else {
        _disp.enqueue(ForwardMsgQI<false>(this, sender,std::move(msg_ptr)));
    }
    if (!indisp) _disp.dispatch();
}



bool LocalBus::is_group(IListener *owner, ChannelID id) const {
    std::shared_lock lk(_mx);
    auto iter =_public_channels.find(id);
    return iter != _public_channels.end() && iter->second->get_owner() == owner;
}

void LocalBus::delivery_error(const Undelivered &msg) {
    _disp.finish();    //finish pending, unlock all locks
    std::unique_lock lk(_mx);

    IListener *lsn = _private_channels.find(msg.sender);
    if (!lsn) lsn = _routing_cache.find_path(msg.sender);
    if (!lsn) {
        auto iter = _public_channels.find(msg.sender);
        if (iter != _public_channels.end()) lsn = iter->second->get_owner();
    }
    if (msg.error == DeliveryError::invalid_target ||  msg.error == DeliveryError::no_route)  {
        _routing_cache.clear_path(msg.target);
    }
    if (lsn) {
        _disp.enqueue(DeliveryErrorQI(lsn, msg, std::move(lk)));
        _disp.dispatch();
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
    _disp.finish();
    std::lock_guard _(_mx);
    _private_channels.erase(listener);

}

void LocalBus::unsubscribe_all(IListener *listener) {
    _disp.finish();
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
    _disp.dispatch();
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

bool LocalBus::add_to_group(IListener *owner, ChannelID group_name, ChannelID uid, ConversationID cid) {
    _disp.finish();
    std::unique_lock lk(_mx);
    IListener *trg = _private_channels.find(uid);
    if (!trg) trg = _routing_cache.find_path(uid);
    if (!trg) return false;
    auto c = _public_channels.create_channel(group_name, owner);
    if (!c) return false;
    c->add(trg);
    _disp.enqueue(AddToGroupQI(trg, group_name, uid, cid, std::move(lk)));
    _disp.dispatch();
    return true;
}

void LocalBus::channel_notify(IChannelNotifyListener *mon, bool enable) {
    _disp.dispatch();
    std::lock_guard lk(_mx);
    _monitors.erase(std::remove(_monitors.begin(), _monitors.end(), mon), _monitors.end());
    if (enable) {
        _monitors.push_back(mon);
    }
}

 void LocalBus::close_all_groups(IListener *owner) {
     _disp.dispatch();
     std::unique_lock<std::shared_mutex> lk(_mx);
     unsubscribe_helper(lk, [&](const Channel<IListener *> &chan) {
         return chan.get_owner() == owner;
     });
 }


 UpdateSerialStatus LocalBus::update_serial(IListener *lsn, const SerialID &serialId) {
    _disp.finish();
    std::unique_lock lk(_mx);
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
    std::shared_lock _(_mx);
    return _cur_serial;
}

Bus Bus::create() {
    return Bus(std::make_shared<LocalBus>());
}


void LocalBus::defer_small_fn(SmallFunction &&fn) {
    _disp.enqueue(std::move(fn));
    _disp.dispatch_if_needed();
}
}
