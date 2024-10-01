#include "bridge.h"
#include <algorithm>
#include <numeric>

namespace zerobus {


std::size_t AbstractBridge::hash_of_channel_list(const ChannelList &list) {
    size_t hash_value = 0;
    std::hash<std::string_view> hasher;

    for (const auto& str_view : list) {
        hash_value ^= hasher(str_view) + 0x9e3779b9 + (hash_value << 6) + (hash_value >> 2);
    }

    return hash_value;
}

namespace {
///Helper class which converts a lambda to an output iterator
template <typename Func>
class LambdaOutputIterator {
public:
    LambdaOutputIterator(Func func) : _fn(func) {}

    template<typename T>
    LambdaOutputIterator& operator=(T &&value) {
        _fn(std::forward<T>(value));
        return *this;
    }
    LambdaOutputIterator& operator*() { return *this; }
    LambdaOutputIterator& operator++() { return *this; }
    LambdaOutputIterator& operator++(int) { return *this; }

private:
    Func _fn;
};

}


AbstractBridge::AbstractBridge(Bus bus)
    :_ptr(std::static_pointer_cast<IBridgeAPI>(bus.get_handle())) {}




void AbstractBridge::send_mine_channels() {
    if (_cycle_detected) {
        _chan_hash = 0;
        send_channels({});
    } else {
        _ptr->get_active_channels(this, [&](ChannelList lst){
           lst = _filter.filter(lst);
           auto h = hash_of_channel_list(lst);
           if (h != _chan_hash) {
               _chan_hash = h;
               send_channels(lst);
           }
        });
    }
}

void AbstractBridge::apply_their_channels(ChannelList lst) {
    std::string_view node_id = _ptr->get_cycle_detect_channel_name();
    std::sort(lst.begin(), lst.end());
    bool cd; {
        auto iter = std::lower_bound(lst.begin(), lst.end(), node_id);
        cd = (iter != lst.end() && *iter == node_id);
    }
    if (cd != _cycle_detected) {
        _cycle_detected = cd;
        send_mine_channels();
    }
    if (cd) lst = {};

    std::set_difference(_cur_channels.begin(), _cur_channels.end(),
                        lst.begin(), lst.end(), LambdaOutputIterator(
                                [&](const ChannelID &id) {
            _ptr->unsubscribe(this, id);
        }));
    std::set_difference(lst.begin(), lst.end(),
                        _cur_channels.begin(), _cur_channels.end(),LambdaOutputIterator(
                                [&](const ChannelID &id) {
            if (_filter.check(id)) _ptr->subscribe(this, id);
        }));
    _char_buffer.resize(std::accumulate(lst.begin(), lst.end(), std::size_t(0),
            [](std::size_t x, const ChannelID &id){return x + id.size();}));
    _cur_channels.resize(lst.size());{
        auto iter = _char_buffer.data();
        std::transform(lst.begin(), lst.end(),_cur_channels.begin(),[&](const ChannelID &id){
            std::string_view ret(iter, id.size());
            iter = std::copy(id.begin(), id.end(), iter);
            return ret;
        });
    }

}

void AbstractBridge::dispatch_message(Message &&msg) {
    if (_filter.check(msg.get_channel())) {
        _ptr->dispatch_message(this, std::move(msg), true);
    }
}

void AbstractBridge::dispatch_message(Message &msg) {
    if (_filter.check(msg.get_channel())) {
        _ptr->dispatch_message(this, std::move(msg), true);
    }
}

AbstractBridge::~AbstractBridge() {
}


void AbstractBridge::peer_reset() {
    _chan_hash = 0;
    send_mine_channels();
}

void AbstractBridge::set_filter(ChannelFilter flt) {
    _filter = std::move(flt);
}

void AbstractBridge::on_message(const Message &message, bool pm) noexcept {
    if (!pm) send_message(message);
}

inline bool check_channel(const std::vector<std::pair<std::string, bool> > &lst, ChannelID id) {
    return std::find_if(lst.begin(), lst.end(), [&](const auto &p) -> bool{
        if (p.second) {
            if (p.first.empty()) return true;
            else return id.substr(0, p.first.size()) == p.first;
        } else {
            return p.first == id;
        }
    }) != lst.end();
}

bool ChannelFilter::check(ChannelID id) const {
    if (!_whitelist.empty() && !check_channel(_whitelist, id)) return false;
    return !check_channel(_blacklist, id);
}

ChannelFilter::ChannelList ChannelFilter::filter(ChannelList lst) const {
    auto iter = std::remove_if(lst.begin(), lst.end(), [&](const ChannelID &id){
        return !check(id);
    });
    return ChannelList(lst.begin(), iter);
}

}
