#include "local_bus.h"
#include <algorithm>
#include <random>
#include <queue>
#include <utility>
#include <atomic>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif


namespace zerobus {





template<typename Iter>
Iter to_base62(std::uint64_t x, Iter iter, int digits = 1) {
    if (x>0 || digits>0) {
        iter = to_base62(x/62, iter, digits-1);
        auto rm = x%62;
        char c = rm < 10?'0'+rm:rm<36?'A'+rm-10:'a'+rm-36;
        *iter = c;
        ++iter;
        return iter;
    }
    return iter;
}

template<typename Iter>
static void generate_mailbox_id(Iter iter) {
    static std::atomic<std::uint64_t> counter = {0};
    std::random_device dev;
    auto rnd = dev();
    auto now = std::chrono::system_clock::now();
    #ifdef _WIN32
        auto pid = GetCurrentProcessId();
    #else
        auto pid= ::getpid();
    #endif
    iter = to_base62(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(), iter);
    iter = to_base62(pid, iter);
    iter = to_base62(counter++,iter,1);
    iter = to_base62(rnd,iter, 1);
}




class LocalBus::MessageDef: public IMessage {
public:
    MessageDef(std::string_view sender, std::string_view channel, std::string_view message, ConversationID cid, std::shared_ptr<LocalBus> owner);
    virtual std::string_view get_sender() const override {return sender;}
    virtual std::string_view get_channel() const override {return channel;}
    virtual MessageContent get_content() const override {return message;}
    virtual ConversationID get_conversation() const override {return cid;}

protected:
    std::vector<char, std::pmr::polymorphic_allocator<char> > data;
    std::string_view sender;
    std::string_view channel;
    std::string_view message;
    ConversationID cid;
    std::shared_ptr<LocalBus> owner;
};

LocalBus::LocalBus()
    :_channels(ChannelMap::allocator_type(&_mem_resource))
    ,_listeners(ListenerToChannelMap::allocator_type(&_mem_resource))
    ,_mailboxes_by_ptr(ListenerToMailboxMap::allocator_type(&_mem_resource))
    ,_mailboxes_by_name(MailboxToListenerMap::allocator_type(&_mem_resource))
    ,_back_path(_mem_resource)
    ,_monitors(mvector<IMonitor *>::allocator_type(&_mem_resource))
    ,_private_queue(PrivateQueue::allocator_type(&_mem_resource))
{

}

LocalBus::PChanMapItem LocalBus::get_channel_lk(ChannelID channel) {
    auto iter = _channels.find(channel);
    if (iter == _channels.end()) {
        auto chan = std::make_shared<ChanDef>(channel, &_mem_resource);
        _channels.emplace(chan->get_id(), chan);
        return chan;
    }
    return iter->second;
}

bool LocalBus::subscribe(IListener *listener, ChannelID channel)
{
    if (channel.empty()) return false;
    std::lock_guard _(*this);
    if (channel == _cycle_detector_id) {
        unsubscribe_all_channels_lk(listener);
        throw CycleDetectedException();
    }
    auto chan = get_channel_lk(channel);
    if (chan->get_owner()) return false;
    chan->add_listener(listener);
    auto iter = _listeners.find(listener);
    if (iter == _listeners.end()) {
        iter = _listeners.emplace(listener, mvector<ChannelID>(mvector<ChannelID>::allocator_type(&_mem_resource))).first;
    }
    iter->second.push_back(chan->get_id());
    _channels_change = true;
    return true;
}

void LocalBus::unsubscribe(IListener *listener, ChannelID channel)
{
    std::lock_guard _(*this);

    if (remove_channel_from_listener_lk(channel,listener)) {
        auto chan = get_channel_lk(channel);
        if (chan->remove_listener(listener)) {
            _channels.erase(channel);
            _channels_change = true;
        }
    }
}

void LocalBus::unsubscribe_all(IListener *listener)
{
    std::lock_guard _(*this);
    erase_mailbox_lk(listener);
    IBridgeListener *blsn = listener->get_bridge();
    if (blsn) {
        _back_path.remove_listener(blsn);
    }
    if (unsubscribe_all_channels_lk(listener)) {
        _channels_change = true;
    }
}

void LocalBus::unsubcribe_private(IListener *listener) {
    std::lock_guard _(*this);
    erase_mailbox_lk(listener);
}

bool LocalBus::unsubscribe_all_channels_lk(IListener *listener) {
    bool ech = false;
    auto iter = _listeners.find(listener);
    if (iter != _listeners.end()) {
        auto &chans = iter->second;
        for (auto &ch: chans) {
            ech = true;
            auto chptr = get_channel_lk(ch);
            if (chptr->remove_listener(listener) || chptr->get_owner() == listener) {
                _channels.erase(ch);
            }
        }
        _listeners.erase(iter);
    }
    return ech;
}

std::string LocalBus::get_random_channel_name(std::string_view prefix) const {
    std::string out(prefix);
    generate_mailbox_id(std::back_inserter(out));
    return out;
}


void LocalBus::erase_mailbox_lk(IListener *listener) {
    //always under lock
    auto iter = _mailboxes_by_ptr.find(listener);
    if (iter == _mailboxes_by_ptr.end()) return;
    _mailboxes_by_name.erase(iter->second);
    _mailboxes_by_ptr.erase(iter);
}

std::string_view LocalBus::get_mailbox(IListener *listener)
{
    static constexpr std::string_view mbx_prefix = "!mbx_";

    std::lock_guard _(*this);
    auto iter = _mailboxes_by_ptr.find(listener);
    if (iter != _mailboxes_by_ptr.end()) return iter->second;
    mstring mbid((mstring::allocator_type(&_mem_resource)));
    mbid.append(mbx_prefix);
    generate_mailbox_id(std::back_inserter(mbid));
    iter = _mailboxes_by_ptr.emplace(listener, std::move(mbid)).first;
    auto ret = _mailboxes_by_name.emplace(iter->second, listener).first->first;
    return ret;
}


Message LocalBus::create_message(ChannelID sender, ChannelID channel, MessageContent msg, ConversationID cid) {
    return Message(std::allocate_shared<MessageDef>(
        std::pmr::polymorphic_allocator<MessageDef>(&_mem_resource),
        std::string(sender), std::string(channel), std::string(msg), cid,
        shared_from_this()
    ));
}
bool LocalBus::send_message(IListener *listener, ChannelID channel, MessageContent message, ConversationID cid)
{
    if (channel.empty()) throw std::invalid_argument("Channel name can't be empty");
    //no lock needed there
    if (listener == nullptr) {
        return forward_message_internal(nullptr, create_message({},channel,message,cid));
    } else {
        return forward_message_internal(listener, create_message(get_mailbox(listener), channel, message, cid));
    }
}

bool LocalBus::dispatch_message(IBridgeListener *listener, Message &&msg, bool subscribe_return_path) {
    if (listener && subscribe_return_path) {
        auto sender = msg.get_sender();
        if (!sender.empty()) {
            std::lock_guard _(*this);
            if (_mailboxes_by_name.find(sender) == _mailboxes_by_name.end()
                   && _channels.find(sender) == _channels.end()) {
                _back_path.store_path(sender, listener);
            }
        }
    }
    return forward_message_internal(listener, std::move(msg));
}

bool LocalBus::remove_channel_from_listener_lk(std::string_view channel, IListener *listener)
{
    auto iter = _listeners.find(listener);
    if (iter == _listeners.end()) return false;
    auto &lst = iter->second;
    auto f = std::find(lst.begin(), lst.end(), channel);
    if (f == lst.end()) return false;
    lst.erase(f);
    if (lst.empty()) {
        _listeners.erase(iter);
    }
    return true;
}

struct LocalBus::TLSQueueItem { // @suppress("Miss copy constructor or assignment operator")
    PChanMapItem channel;
    Message msg;
    IListener *listener;
};

struct LocalBus::TLState {

    std::queue<TLSQueueItem> _queue;
    void run_queue(TLSQueueItem item) {
        auto can_run = _queue.empty();
        _queue.push(std::move(item));
        if (can_run) {
            while (!_queue.empty()) {
                TLSQueueItem &x = _queue.front();
                x.channel->enum_listeners([&x](IListener *l){
                    if (l != x.listener) l->on_message(x.msg, false);
                });
                _queue.pop();
            }
        }
    }

    static thread_local TLState _tls_state;
};

thread_local LocalBus::TLState LocalBus::TLState::_tls_state = {};


bool LocalBus::forward_message_internal(IListener *listener,  Message &&msg) {
    PChanMapItem ch;
    ChannelID chanid = msg.get_channel();

    do{
        //mailboxes have priority (user cannot choose own mailbox name)
        auto miter = _mailboxes_by_name.find(chanid);
        if (miter != _mailboxes_by_name.end()) {
            auto l = miter->second;
            run_priv_queue(l, std::move(msg), true);
            return true;
        }

        //channels have priority over return path
        //because return path could contain channel name to steal communication
        std::lock_guard _(*this);
        auto citer = _channels.find(chanid);
        if (citer != _channels.end()) {
            auto own = citer->second->get_owner();
            if (own == listener || own == nullptr) {
                ch = citer->second;
                break;
            }
        }


        //if no path found, route to return path
        IListener *bpath = _back_path.find_path(chanid);
        if (bpath) {
            run_priv_queue(bpath, std::move(msg), true);
            return true;
        }

        //now we cannot route the message
        return false;

    } while (false);

    //process channel outside of lock (has own lock)
    TLState::_tls_state.run_queue({std::move(ch), std::move(msg), std::move(listener)});
    return true;
}

void LocalBus::force_update_channels() {
    std::lock_guard _(*this);
    _channels_change = true;
}

bool LocalBus::add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) {
    std::lock_guard _(*this);

    auto new_channel = [&](auto lsn){
        auto ch = get_channel_lk(group_name);
        auto own = ch->get_owner();
        if (own != nullptr && own != owner) return false;
        ch->set_owner(owner);

        ch->add_listener(lsn);
        return true;
    };

    auto iter = _mailboxes_by_name.find(uid);
    if (iter == _mailboxes_by_name.end()) {

        IBridgeListener *lsn = _back_path.find_path(uid);
        if (lsn == nullptr) return false;
        if (!new_channel(lsn)) return false;
        lsn->on_add_to_group(group_name, uid);
        return true;
    } else {

        if (!new_channel(iter->second)) return false;
        IBridgeListener *br = iter->second->get_bridge();
        if (br) br->on_add_to_group(group_name, uid);
        return true;

    }
}

void LocalBus::close_group(IListener *owner, ChannelID group_name) {
    auto citer = _channels.find(group_name);
    if (citer != _channels.end()) {
        if (citer->second->get_owner() == owner) {
            _channels.erase(citer);
            _channels_change = true;
        }
    }
}

void LocalBus::run_priv_queue(IListener *target, Message &&msg, bool pm) {
    bool run = _private_queue.empty();
    _private_queue.push_back({target,  std::move(msg), pm});
    if (run) {
        while (!_private_queue.empty()) {
            auto &x = _private_queue.front();
            x.target->on_message(x.msg, pm);
            _private_queue.pop_front();
        }
    }

}

void LocalBus::register_monitor(IMonitor *mon) {
    std::lock_guard _(*this);
    _monitors.push_back(mon);
}

void LocalBus::unregister_monitor(const IMonitor *mon) {
    std::lock_guard _(*this);
    auto iter = std::find(_monitors.begin(), _monitors.end(), mon);
    if (iter != _monitors.end()) {
        std::swap(*iter, _monitors.back());
        _monitors.pop_back();
    }
}


void LocalBus::get_active_channels(IListener *listener,FunctionRef<void(ChannelList)> &&cb) const {
    std::lock_guard _(*this);
    _tmp_channels.clear();
    if (_last_proxy && listener != _last_proxy) {
        if (_cycle_detector_id.empty()) {
            _cycle_detector_id = get_random_channel_name(cycle_detection_prefix);
        }
    } else {
        _last_proxy = listener;
    }
    for (const auto &[k,v]: _channels) {
        if (v->can_export(listener)) {
            _tmp_channels.push_back(k);
        }
    }
    if (!_cycle_detector_id.empty()) {
        _tmp_channels.push_back(_cycle_detector_id);
    }

    cb({_tmp_channels.begin(), _tmp_channels.end()});
}

void LocalBus::get_subscribed_channels(IListener *listener,FunctionRef<void(ChannelList)> &&cb) const {
    std::lock_guard _(*this);
    auto iter = _listeners.find(listener);
    if (iter == _listeners.end()) return;
    _tmp_channels.clear();
    _tmp_channels.resize(iter->second.size());
    std::copy(iter->second.begin(), iter->second.end(), _tmp_channels.begin());
    cb({_tmp_channels.begin(), _tmp_channels.end()});
}

LocalBus::ChanDef::ChanDef(std::string_view name, std::pmr::memory_resource *memres)
    :_name(name, std::pmr::polymorphic_allocator<char>(memres))
    ,_listeners(std::pmr::polymorphic_allocator<std::pair<IListener *, bool> >(memres)) {}

LocalBus::ChanDef::~ChanDef() {
    enum_listeners([&](IListener *lsn){
        auto br = lsn->get_bridge();
        if (br) br->on_close_group(_name);
    });
}

void LocalBus::ChanDef::lock() {
    _mx.lock();
    ++_recursion;
}

void LocalBus::ChanDef::unlock() {
    --_recursion;
    if (!_recursion && _del_count) {
        auto iter = std::remove(_listeners.begin(), _listeners.end(), nullptr);
        _listeners.erase(iter, _listeners.end());
        _del_count = 0;
    }
    _mx.unlock();
}

template<std::invocable<IListener *> Fn>
void LocalBus::ChanDef::enum_listeners(Fn &&fn) {
    std::lock_guard _(*this);
    //using numeric counter is intentional
    //underlying array can resize self which renders iterators unusable
    for (std::size_t i = 0; i < _listeners.size(); ++i) {
        auto l = _listeners[i];
        if (l) fn(l);
    }
}

bool LocalBus::ChanDef::empty() const {
    std::lock_guard _(_mx);
    return (_listeners.size() - _del_count) == 0;
}

void LocalBus::ChanDef::add_listener(IListener *lsn) {
    std::lock_guard _(*this);
    _listeners.push_back(lsn);
}

bool LocalBus::ChanDef::remove_listener(IListener *lsn) {
    auto iter = std::find(_listeners.begin(), _listeners.end(), lsn);
    if (iter != _listeners.end()) {
        *iter = nullptr;
        ++_del_count;
    }
    return (_listeners.size() - _del_count) == 0;

}

bool LocalBus::ChanDef::can_export(IListener *lsn) const {
    std::lock_guard _(_mx);
    if (_owner) return false; //group is not exportable
    auto iter = std::find_if(_listeners.begin(), _listeners.end(), [&](const auto &l){
        return l && l != lsn;
    });
    return iter != _listeners.end();
}

ChannelID LocalBus::ChanDef::get_id() const {
    return _name; //no lock is needed (it is immutable)
}

Bus LocalBus::create() {
    return Bus(std::make_shared<LocalBus>());
}

bool LocalBus::is_channel(ChannelID id) const {
    std::lock_guard _(*this);
    auto iter = _channels.find(id);
    return iter != _channels.end() && !iter->second->empty();
}

LocalBus::MessageDef::MessageDef(std::string_view sender,
        std::string_view channel, std::string_view message, ConversationID cid,
        std::shared_ptr<LocalBus> owner)
:data(std::pmr::polymorphic_allocator<char>(&owner->_mem_resource))
,cid(cid)
,owner(std::move(owner))
{
    //we use polymorphic allocator to allocate one space for all three strings
    //calculate total size (+ 3times terminating zero)
    auto needsz = sender.size()+channel.size()+message.size()+3;
    //allocate buffer
    data.resize(needsz,0);
    //copy each string to buffer
    //construct string_view
    //append zero
    auto iter = data.data();
    this->sender = std::string_view(iter, sender.size());
    iter = std::copy(sender.begin(), sender.end(), iter);
    *iter++ = 0;
    this->channel= std::string_view(iter, channel.size());
    iter = std::copy(channel.begin(), channel.end(), iter);
    *iter++ = 0;
    this->message= std::string_view(iter, message.size());
    iter = std::copy(message.begin(), message.end(), iter);
    *iter++ = 0;
}

LocalBus::BackPathStorage::BackPathStorage(std::pmr::memory_resource &res)
:_entries(BackPathMap::allocator_type(&res))
{
    _root = reinterpret_cast<BackPathItem *>(&_last);
}


void LocalBus::BackPathItem::remove() {
    if (prev) prev->next = next;
    if (next) next->prev = prev;

}
void LocalBus::BackPathItem::promote(BackPathItem * &root) {
    if (root != this) {
        remove();
        prev = nullptr;
        next = root;
        root->prev = this;
        root = this;
    }
}

void LocalBus::BackPathStorage::store_path(const ChannelID &chan, IBridgeListener *lsn) {
    auto iter = _entries.find(chan);
    if (iter == _entries.end()) {
        if (lsn == nullptr) return;
        mvector<char> name(chan.begin(), chan.end(), mvector<char>::allocator_type(_entries.get_allocator()));
        std::string_view key(name.data(), name.size());
        auto iter = _entries.emplace(key, BackPathItem{
            nullptr, nullptr, std::move(name), lsn}).first;
        iter->second.promote(_root);
        while (_entries.size() > _limit) {
            auto *l = _last;
            l->remove();
            _entries.erase(std::string_view(l->id.begin(), l->id.end()));
        }
    } else if (lsn == nullptr) {
        iter->second.remove();
        _entries.erase(iter);
    } else {
        iter->second.l = lsn;
        iter->second.promote(_root);
    }
}

IBridgeListener* LocalBus::BackPathStorage::find_path(const ChannelID &chan) const {
    auto iter = _entries.find(chan);
    if (iter != _entries.end()) return iter->second.l;
    return nullptr;
}


bool LocalBus::clear_return_path(IBridgeListener *lsn, ChannelID sender, ChannelID receiver)  {
    std::lock_guard _(_mutex);
    auto lsn2 = _back_path.find_path(receiver);
    if (lsn == lsn2) {
        _back_path.store_path(receiver, nullptr);
        auto lsn3 = _back_path.find_path(sender);
        if (lsn3) {
            lsn3->on_clear_path(sender, receiver);
        }
        return true;
    }
    auto iter = _mailboxes_by_name.find(sender);
    if (iter != _mailboxes_by_name.end()) {
        auto br = iter->second->get_bridge();
        if (br) br->on_clear_path(sender, receiver);
    }


    return false;
}

void LocalBus::BackPathStorage::remove_listener(IBridgeListener *l) {
    auto *ptr = _root;
    while (ptr != reinterpret_cast<BackPathItem *>(&_last)) {
        auto x = ptr;
        ptr = ptr->next;
        if (ptr->l == l) {
            x->remove();
            _entries.erase(std::string_view(x->id.begin(), x->id.end()));
        }
    }
}

std::string_view LocalBus::get_cycle_detect_channel_name() const {
    return _cycle_detector_id;
}

Bus Bus::create() {
    return Bus(std::make_shared<LocalBus>());
}

void LocalBus::lock() const {
    _mutex.lock();
    ++_recursion;
}
void LocalBus::unlock() const {
    if (_recursion == 1) {
        while (_channels_change) {
            _channels_change = false;
            for (const auto &m: _monitors) m->on_channels_update();
        }
    }
    --_recursion;
    _mutex.unlock();
}


}
