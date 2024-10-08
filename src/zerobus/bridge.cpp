#include "bridge.h"
#include <algorithm>
#include <numeric>

namespace zerobus {



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

    auto flt = _filter.load();
    if (flt) {
        auto e = std::remove_if(lst.begin(), lst.end(), [&](const ChannelID ch){
           if (ch.substr(0,4) == "cdp_") return true;   //allow cdp_ channels
           return !flt->incoming(ch); //we block anounincing channel to prevent incoming message
        });
        lst = ChannelList(lst.begin(), e);
    }

    if (_cur_channels.empty()) {
        if (lst.empty()) return;
        send_channels(lst, Operation::replace);
    } else {


        std::sort(lst.begin(), lst.end());


        std::set_difference(lst.begin(), lst.end(),
                _cur_channels.begin(), _cur_channels.end(), std::back_inserter(_tmp));
        if (!_tmp.empty()) send_channels(_tmp, Operation::add);
        _tmp.clear();

        std::set_difference(_cur_channels.begin(), _cur_channels.end(),
                lst.begin(), lst.end(), std::back_inserter(_tmp));
        if (!_tmp.empty()) send_channels(_tmp, Operation::erase);
        _tmp.clear();
    }
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
                    cycle_detection(_cycle_detected);
                    send_reset();
                    _ptr->force_update_channels();
                    return;
                }
            } else {
                if (!_cycle_detected) {
                    _cycle_detected = true;
                    cycle_detection(_cycle_detected);
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
        case Operation::add: {
            auto flt = _filter.load();
            if (flt) {
                for (const auto &x: lst) {
                    //
                    if (x.substr(0,IBridgeAPI::cycle_detection_prefix.size())
                            == IBridgeAPI::cycle_detection_prefix || flt->outgoing(x))
                        _ptr->subscribe(this, x);
                }
            } else {
                for (const auto &x: lst) _ptr->subscribe(this, x);
            }
        }
        break;
        case Operation::erase: for (const auto &x: lst) _ptr->unsubscribe(this, x);
                               break;
    }
}

void AbstractBridge::apply_their_reset() {
    if (_cur_channels.empty()) return; //no active channels - do nothing
    //enforce refresh
    _cur_channels.clear();
    _ptr->force_update_channels();
}

void AbstractBridge::dispatch_message(Message &&msg) {
    dispatch_message(msg);
}

void AbstractBridge::dispatch_message(Message &msg) {
    auto flt = _filter.load();
    if (flt) {
        auto ch = msg.get_channel();
        //if ch is not channel, then it is mailbox or response (cannot be filtered)
        if (_ptr->is_channel(ch) && !flt->incoming(ch)) return; //block message
    }
    if (!_ptr->dispatch_message(this, std::move(msg), true)) {
        on_clear_path(msg.get_sender(), msg.get_channel());
    }
}

AbstractBridge::~AbstractBridge() {
    _ptr->unsubscribe_all(this);
    auto flt = _filter.load();
    delete flt;
}



void AbstractBridge::set_filter(std::unique_ptr<IChannelFilter> &&flt) {
    auto r = _filter.exchange(flt.release());
    flt.reset(r);
}

void AbstractBridge::apply_their_clear_path(ChannelID sender, ChannelID receiver) {
    _ptr->clear_return_path(this, sender, receiver);
}

void AbstractBridge::on_message(const Message &message, bool pm) noexcept {
    auto flt = _filter.load();
    if (flt) {
        //block message if it is not personal message (response) or not allowed channel
        if (!pm && !flt->outgoing(message.get_channel())) return;
    }
    send_message(message);
}



AbstractBridge::ChannelList AbstractBridge::persist_channel_list(const ChannelList &source, std::vector<ChannelID> &channels, std::vector<char> &characters) {
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

bool IChannelFilter::incoming(ChannelID ) const {return true;}
bool IChannelFilter::outgoing(ChannelID) const {return true;}

void AbstractBridge::apply_their_close_group(ChannelID group_name) {
    _ptr->close_group(group_name);
}

void AbstractBridge::apply_their_add_to_group(ChannelID group_name, ChannelID target_id) {
    if (!_ptr->add_to_group(group_name, target_id)) {
        send_channels(ChannelList(&group_name,1), Operation::erase);
    }
}

}
