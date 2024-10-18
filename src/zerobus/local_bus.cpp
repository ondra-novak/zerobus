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


template<bool ref>
struct LocalBus::TLSMsgQueueItem { // @suppress("Miss copy constructor or assignment operator")
    std::conditional_t<ref, const PTargetMapItem &, PTargetMapItem> channel;
    std::conditional_t<ref, const Message &, Message> message;
    IListener *listener;

    operator TLSMsgQueueItem<false>() const {
        return {channel,  message, listener};
    }

    void execute() const noexcept {
        channel->broadcast(listener, message);
    }
};

struct LocalBus::TLSLsnQueueItem {
    PChanMapItem chan;
    IListener *lsn;
    std::shared_ptr<LocalBus> owner;

    void execute() const noexcept {
        if (owner) {
            if (chan) {
                if (chan->remove_listener(lsn)) {
                    owner->channel_is_empty(chan->get_id());
                }
            } else {
                owner->remove_mailbox(lsn);
            }
        } else {
            chan->add_listener(lsn);
        }
    }
};


struct LocalBus::TLState {

    std::queue<TLSMsgQueueItem<false> > _msg_queue;
    std::queue<TLSLsnQueueItem> _lsn_queue;
    bool _running = false;


    void enqueue_msg(const TLSMsgQueueItem<true> &item) {
        if (_running) {
            _msg_queue.emplace(item);
        } else {
            _running = true;
            item.execute();
            run_msg_queue();
            _running = false;
        }
    }

    void enqueue_lsn(TLSLsnQueueItem &&item) {
        if (_running) {
            _lsn_queue.emplace(std::move(item));
        } else {
            _running = true;
            item.execute();
            run_lsn_queue();
            _running = false;
        }
    }

    void run_lsn_queue() {
        while (!_lsn_queue.empty()) {
            _lsn_queue.front().execute();
            _lsn_queue.pop();
        }
    }


    void run_msg_queue() {
        run_lsn_queue();
        while (!_msg_queue.empty()) {
            _msg_queue.front().execute();
            _msg_queue.pop();
            run_lsn_queue();
        }
    }


    static thread_local TLState _tls_state;
};




template<typename Iter>
Iter to_base62(std::uint64_t x, Iter iter, int digits = 1) {
    if (x>0 || digits>0) {
        iter = to_base62(x/62, iter, digits-1);
        auto rm = x%62;
        char c = static_cast<char>(rm < 10?'0'+rm:rm<36?'A'+rm-10:'a'+rm-36);
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






LocalBus::LocalBus()
    :_channels(ChannelMap::allocator_type(&_mem_resource))
    ,_mailboxes_by_ptr(ListenerToMailboxMap::allocator_type(&_mem_resource))
    ,_mailboxes_by_name(MailboxToListenerMap::allocator_type(&_mem_resource))
    ,_back_path(_mem_resource)
    ,_monitors(mvector<IMonitor *>::allocator_type(&_mem_resource))
    ,_this_serial(LocalBus::get_random_channel_name(""))
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
    auto chan = get_channel_lk(channel);
    if (chan->get_owner()) return false;
    TLState::_tls_state.enqueue_lsn({std::move(chan), listener, {}});
    _channels_change = true;
    return true;
}

void LocalBus::unsubscribe(IListener *listener, ChannelID channel)
{
    std::lock_guard _(*this);

    auto iter = _channels.find(channel);
    if (iter == _channels.end()) return;
    auto ch = iter->second;
    if (ch->has(listener)) {
        TLState::_tls_state.enqueue_lsn({std::move(ch), listener, shared_from_this()});
        _channels_change = true;
    }
}

void LocalBus::channel_is_empty(ChannelID id) {
    std::lock_guard _(*this);
    _channels.erase(id);
}

bool LocalBus::set_serial(IListener *lsn, SerialID serialId) {
    std::lock_guard _(*this);
    if (serialId.empty()) return true;
    SerialID cur_id = _serial_source?_cur_serial:_this_serial;
    if (serialId == cur_id) {
        return lsn == _serial_source;
    }
    if (cur_id > serialId) {
        _serial_source = lsn;
        _cur_serial = serialId;
    }
    return true;
}

SerialID LocalBus::get_serial(IListener *lsn) const {
    std::lock_guard _(*this);
    if (_serial_source) {
        if (lsn != _serial_source) return _cur_serial;
        else return "";
    }
    return _this_serial;
}

void LocalBus::remove_mailbox(IListener *lsn) {
    PMBxDef def;
    std::lock_guard _(*this);
    auto iter = _mailboxes_by_ptr.find(lsn);
    if (iter == _mailboxes_by_ptr.end()) return;
    _mailboxes_by_name.erase(iter->second->get_id());
    def = std::move(iter->second);
    _mailboxes_by_ptr.erase(iter);
}

void LocalBus::unsubscribe_all(IListener *listener)
{
    std::lock_guard _(*this);
    erase_mailbox_lk(listener);
    erase_groups_lk(listener);
    _back_path.remove_listener(listener);
    if (unsubscribe_all_channels_lk(listener, true)) {
        _channels_change = true;
    }
    if (listener == _serial_source) {
        _serial_source = nullptr;
        _channels_change = true;
    }
}

void LocalBus::unsubcribe_private(IListener *listener) {
    std::lock_guard _(*this);
    erase_mailbox_lk(listener);
}



void LocalBus::unsubscribe_all_channels(IListener *listener, bool and_groups) {
    std::lock_guard _(*this);
    if (unsubscribe_all_channels_lk(listener, and_groups)) {
        _channels_change = true;
    }
}

bool LocalBus::unsubscribe_all_channels_lk(IListener *listener, bool and_groups) {
    bool ech = false;
    PChanMapItem *lst = reinterpret_cast<PChanMapItem *>(alloca(sizeof(PChanMapItem)*_channels.size()));
    PChanMapItem *iter = lst;
    for (const auto &[k,ch]: _channels) {
        auto owner = ch->get_owner();
        if ((and_groups || owner == nullptr) && ch->has(listener)) {
            std::construct_at(iter, ch);
            ++iter;
            ech = true;
        }
    }
    for (auto x = lst; x != iter; ++x) {
        TLState::_tls_state.enqueue_lsn({std::move(*x), listener, shared_from_this()});
        std::destroy_at(x);
    }
    return ech;

}

std::string LocalBus::get_random_channel_name(std::string_view prefix) const {
    std::string out(prefix);
    generate_mailbox_id(std::back_inserter(out));
    return out;
}

void LocalBus::close_all_groups(IListener *owner) {
    std::lock_guard _(*this);
    erase_groups_lk(owner);
}

void LocalBus::erase_groups_lk(IListener *owner) {
    for (auto iter = _channels.begin(); iter != _channels.end();) {
        auto &ch = *iter->second;
        if (ch.get_owner() == owner) {
            iter = _channels.erase(iter);
        } else {
            ++iter;
        }
    }
}
void LocalBus::erase_mailbox_lk(IListener *listener) {
    //always under lock
    auto iter = _mailboxes_by_ptr.find(listener);
    if (iter == _mailboxes_by_ptr.end()) return;
    iter->second->disable();
    TLState::_tls_state.enqueue_lsn({{},listener,shared_from_this()});
}

std::string_view LocalBus::get_mailbox(IListener *listener)
{
    static constexpr std::string_view mbx_prefix = "mbx_";

    std::lock_guard _(*this);
    auto iter = _mailboxes_by_ptr.find(listener);
    if (iter != _mailboxes_by_ptr.end()) return iter->second->get_id();
    mstring mbid((mstring::allocator_type(&_mem_resource)));
    mbid.append(mbx_prefix);
    generate_mailbox_id(std::back_inserter(mbid));
    auto mbx = std::allocate_shared<MbxDef>(
            std::pmr::polymorphic_allocator<MbxDef>(&_mem_resource),
            listener, std::move(mbid));
    std::string_view idstr = mbx->get_id();
    _mailboxes_by_ptr.emplace(listener, mbx);
    _mailboxes_by_name.emplace(idstr, mbx);
    return idstr;
}


bool LocalBus::send_message(IListener *listener, ChannelID channel, MessageContent message, ConversationID cid)
{
    if (channel.empty()) throw std::invalid_argument("Channel name can't be empty");
    //no lock needed there
    if (listener == nullptr) {
        return forward_message_internal(nullptr, Message({},channel,message,cid));
    } else {
        auto s = get_mailbox(listener);
        char *c = reinterpret_cast<char *>(alloca(s.size()));       //copy sender to stack - can be removed during processing
        std::copy(s.begin(), s.end(), c);
        s = {c, s.size()};
        return forward_message_internal(listener, Message(s, channel, message, cid));
    }
}

bool LocalBus::dispatch_message(IListener *listener, const Message &msg, bool subscribe_return_path) {
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





thread_local LocalBus::TLState LocalBus::TLState::_tls_state = {};


bool LocalBus::forward_message_internal(IListener *listener,  const Message &msg) {
    PTargetMapItem ch;
    ChannelID chanid = msg.get_channel();

    do{
        //mailboxes have priority (user cannot choose own mailbox name)
        auto miter = _mailboxes_by_name.find(chanid);
        if (miter != _mailboxes_by_name.end()) {
            ch = miter->second;
            break;
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
            bpath->on_message(msg, true);
            return true;
        }

        //now we cannot route the message
        return false;

    } while (false);

    //process channel outside of lock (has own lock)
    TLState::_tls_state.enqueue_msg({ch, msg, listener});
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

        IListener *lsn = _back_path.find_path(uid);
        if (lsn == nullptr) return false;
        if (!new_channel(lsn)) return false;
        lsn->on_add_to_group(group_name, uid);
        return true;
    } else {

        if (!new_channel(iter->second->get_owner())) return false;
        iter->second->get_owner()->on_add_to_group(group_name, uid);
        return true;

    }
}

void LocalBus::close_group(IListener *owner, ChannelID group_name) {
    auto citer = _channels.find(group_name);
    if (citer != _channels.end()) {
        if (citer->second->get_owner() == owner) {
            citer->second->set_owner(nullptr);
            _channels.erase(citer);
            _channels_change = true;
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


LocalBus::ChannelList LocalBus::get_active_channels(IListener *listener,ChannelListStorage &storage) const {
    std::lock_guard _(*this);
    storage.clear();
    for (const auto &[k,v]: _channels) {
        if (v->can_export(listener)) {
            storage._channels.push_back(k);
            storage._locks.emplace_back(v, nullptr);
        }
    }
    return storage.get_channels();
}

LocalBus::ChannelList LocalBus::get_subscribed_channels(IListener *listener, ChannelListStorage &storage) const {
    std::lock_guard _(*this);
    storage.clear();
    for (const auto &[k,v]: _channels) {
        if (v->has(listener)) {
            storage._channels.push_back(k);
            storage._locks.emplace_back(v, nullptr);
        }
    }
    return storage.get_channels();
}

LocalBus::MbxDef::MbxDef(IListener *lsn, mstring id):_owner(lsn),_id(std::move(id)) {

}

void LocalBus::MbxDef::disable() {
    _disabled = true;
}

void LocalBus::MbxDef::broadcast(IListener *, const Message &msg) const {
    if (_disabled) return ;
    _owner->on_message(msg, true);
}

LocalBus::ChanDef::ChanDef(std::string_view name, std::pmr::memory_resource *memres)
    :_name(name, std::pmr::polymorphic_allocator<char>(memres))
    ,_listeners(std::pmr::polymorphic_allocator<std::pair<IListener *, bool> >(memres)) {}

LocalBus::ChanDef::~ChanDef() {
    for (auto lsn: _listeners) lsn->on_close_group(_name);
    if (_owner) _owner->on_group_empty(_name); //clear group
}


void LocalBus::ChanDef::broadcast(IListener *lsn, const Message &msg) const {
    std::shared_lock _(_mx);
    for (const auto &l: _listeners) {
        if (l != lsn) l->on_message(msg, false);
    }
}

bool LocalBus::ChanDef::empty() const {
    std::shared_lock _(_mx);
    return _listeners.empty();
}

void LocalBus::ChanDef::add_listener(IListener *lsn) {
    std::unique_lock _(_mx);
    auto iter = std::lower_bound(_listeners.begin(), _listeners.end(), lsn);
    if (iter != _listeners.end() && *iter  == lsn) return;
    _listeners.insert(iter, lsn);
}

bool LocalBus::ChanDef::remove_listener(IListener *lsn) {
    std::unique_lock lk(_mx);
    auto iter = std::lower_bound(_listeners.begin(), _listeners.end(), lsn);
    if (iter != _listeners.end() && *iter == lsn) {
        _listeners.erase(iter);
    }
    return  _listeners.empty();

}

bool LocalBus::ChanDef::has(IListener *lsn) const {
    std::shared_lock _(_mx);
    auto iter = std::lower_bound(_listeners.begin(), _listeners.end(), lsn);
    return (iter != _listeners.end() && *iter  == lsn);
}

bool LocalBus::ChanDef::can_export(IListener *lsn) const {
    std::shared_lock _(_mx);
    if (_owner) return false; //group is not exportable
    if (_listeners.empty()) return false;   //don't export empty channels
    return _listeners.size() > 1 || _listeners[0] != lsn;
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

void LocalBus::BackPathStorage::store_path(const ChannelID &chan, IListener *lsn) {
    auto iter = _entries.find(chan);
    if (iter == _entries.end()) {
        if (lsn == nullptr) return;
        mvector<char> name(chan.begin(), chan.end(), mvector<char>::allocator_type(_entries.get_allocator()));
        std::string_view key(name.data(), name.size());
        auto iter2 = _entries.emplace(key, BackPathItem{
            nullptr, nullptr, std::move(name), lsn}).first;
        iter2->second.promote(_root);
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

IListener* LocalBus::BackPathStorage::find_path(const ChannelID &chan) const {
    auto iter = _entries.find(chan);
    if (iter != _entries.end()) return iter->second.l;
    return nullptr;
}


bool LocalBus::clear_return_path(IListener *lsn, ChannelID sender, ChannelID receiver)  {
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
    {
        auto iter = _mailboxes_by_name.find(sender);
        if (iter != _mailboxes_by_name.end()) {
            iter->second->get_owner()->on_clear_path(sender, receiver);
        }
    }

    return false;
}

void LocalBus::BackPathStorage::remove_listener(IListener *l) {
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
