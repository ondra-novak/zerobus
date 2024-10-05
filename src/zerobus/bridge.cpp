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




void AbstractBridge::process_mine_channels(ChannelList lst) noexcept {
    lst = _filter.filter(lst);

    std::sort(lst.begin(), lst.end());


    std::set_difference(lst.begin(), lst.end(),
            _cur_channels.begin(), _cur_channels.end(), std::back_inserter(_tmp));
    if (!_tmp.empty()) send_channels(_tmp, Operation::add);
    _tmp.clear();

    std::set_difference(_cur_channels.begin(), _cur_channels.end(),
            lst.begin(), lst.end(), std::back_inserter(_tmp));
    if (!_tmp.empty()) send_channels(_tmp, Operation::erase);
    _tmp.clear();

    persist_channel_list(lst, _cur_channels, _char_buffer);
}

void AbstractBridge::send_mine_channels() {
    if (!_cycle_detected) {
        _ptr->get_active_channels(this, [&](auto lst){process_mine_channels(lst);});
    } else {
        process_mine_channels({});
    }
}

void AbstractBridge::apply_their_channels(ChannelList lst, Operation op) {
    auto cdid = _ptr->get_cycle_detect_channel_name();
    if (!cdid.empty()) {
        auto iter = std::find(lst.begin(), lst.end(), cdid);
        if (iter != lst.end()) {
            if (op == Operation::erase) {
                if (_cycle_detected) {
                    _cycle_detected = false;
                    send_reset();
                    return;
                }
            } else {
                if (!_cycle_detected) {
                    _cycle_detected = true;
                    _ptr->unsubscribe_all(this);
                    return;
                }
            }
        }
    }
    if (_cycle_detected) return;
    switch (op) {
        case Operation::replace: _ptr->unsubscribe_all(this);
                                 [[fallthrough]];
        case Operation::add: for (const auto &x: lst) _ptr->subscribe(this, x);
                             break;
        case Operation::erase: for (const auto &x: lst) _ptr->unsubscribe(this, x);
                               break;
    }
}

void AbstractBridge::apply_their_reset() {
    _cur_channels.clear();
    _char_buffer.clear();
    send_mine_channels();
}

void AbstractBridge::dispatch_message(Message &&msg) {
    dispatch_message(msg);
}

void AbstractBridge::dispatch_message(Message &msg) {
    if (_filter.check(msg.get_channel())) {
        if (!_ptr->dispatch_message(this, std::move(msg), true)) {
            send_clear_path(msg.get_sender(), msg.get_channel());
        }
    }
}

AbstractBridge::~AbstractBridge() {
    _ptr->unsubscribe_all(this);
}



void AbstractBridge::set_filter(ChannelFilter flt) {
    _filter = std::move(flt);
}

void AbstractBridge::apply_their_clear_path(ChannelID sender, ChannelID receiver) {
    _ptr->clear_return_path(this, receiver);
    follow_return_path(sender, [&](AbstractBridge *brd){
        brd->send_clear_path(sender, receiver);
    });
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

ChannelFilter::ChannelList AbstractBridge::persist_channel_list(const ChannelList &source, std::vector<ChannelID> &channels, std::vector<char> &characters) {
    characters.clear();
    channels.clear();
    std::size_t needsz = std::accumulate(source.begin(), source.end(), std::size_t(0), [&](auto cnt, const auto &str){
        return cnt + str.size();
    });
    characters.resize(needsz);
    channels.resize(source.size());
    auto iter = characters.data();
    std::transform(source.begin(), source.end(), channels.begin(),[&](const ChannelID &id){
        std::string_view ret(iter, id.size());
        iter = std::copy(id.begin(), id.end(), iter);
        return ret;
    });
    return channels;

}

}
