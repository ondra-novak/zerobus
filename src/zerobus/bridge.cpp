#include "bridge.h"
#include <algorithm>
#include <mutex>
#include <numeric>

namespace zerobus {




AbstractBridge::AbstractBridge(Bus bus)
    :_ptr(std::static_pointer_cast<IBridgeAPI>(bus.get_handle())) {}




void AbstractBridge::process_mine_channels(ChannelList lst) noexcept {

    auto flt = _filter.load();
    if (flt) {
        auto e = std::remove_if(lst.begin(), lst.end(), [&](const ChannelID ch){
           if (ch.substr(0,4) == "cdp_") return true;   //allow cdp_ channels
           return !flt->on_incoming(ch); //we block anounincing channel to prevent incoming message
        });
        lst = ChannelList(lst.begin(), e);
    }

    if (_cur_channels.empty()) {
        if (lst.empty()) return;
        send(ChannelUpdate{lst, Operation::replace});
    } else {


//        std::sort(lst.begin(), lst.end());
//      no sort needed, it is already sorted


        std::set_difference(lst.begin(), lst.end(),
                _cur_channels.begin(), _cur_channels.end(), std::back_inserter(_tmp));
        if (!_tmp.empty()) send(ChannelUpdate{_tmp, Operation::add});
        _tmp.clear();

        std::set_difference(_cur_channels.begin(), _cur_channels.end(),
                lst.begin(), lst.end(), std::back_inserter(_tmp));
        if (!_tmp.empty()) send(ChannelUpdate{_tmp, Operation::erase});
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

void AbstractBridge::receive(const ChannelUpdate &chan_up) {
    auto cdid = _ptr->get_cycle_detect_channel_name();
    if (!cdid.empty()) {
        auto iter = std::find(chan_up.lst.begin(), chan_up.lst.end(), cdid);
        if (iter != chan_up.lst.end()) {
            if (chan_up.op == Operation::erase) {
                if (_cycle_detected) {
                    _cycle_detected = false;
                    cycle_detection(_cycle_detected);
                    send(ChannelReset{});
                    _ptr->force_update_channels();
                    return;
                }
            } else {
                if (!_cycle_detected) {
                    _cycle_detected = true;
                    cycle_detection(_cycle_detected);
                    _ptr->unsubscribe_all_channels(this);
                    return;
                }
            }
        }
    }
    if (_cycle_detected) return;
    switch (chan_up.op) {
        case Operation::replace: _ptr->unsubscribe_all_channels(this);
                                 [[fallthrough]];
        case Operation::add: {
            auto flt = _filter.load();
            if (flt) {
                for (const auto &x: chan_up.lst) {
                    //
                    if (x.substr(0,IBridgeAPI::cycle_detection_prefix.size())
                            == IBridgeAPI::cycle_detection_prefix || flt->on_outgoing(x))
                        _ptr->subscribe(this, x);
                }
            } else {
                for (const auto &x: chan_up.lst) _ptr->subscribe(this, x);
            }
        }
        break;
        case Operation::erase: for (const auto &x: chan_up.lst) _ptr->unsubscribe(this, x);
                               break;
    }
}

void AbstractBridge::receive(ChannelReset) {
    if (_cur_channels.empty()) return; //no active channels - do nothing
    //enforce refresh
    _cur_channels.clear();
    _ptr->force_update_channels();
}


void AbstractBridge::receive(const Message &msg) {
    auto flt = _filter.load();
    if (flt) {
        auto ch = msg.get_channel();
        //if ch is not channel, then it is mailbox or response (cannot be filtered)
        if (_ptr->is_channel(ch) && !flt->on_incoming(ch)) return; //block message
    }
    if (!_ptr->dispatch_message(this, msg, true)) {
        on_clear_path(msg.get_sender(), msg.get_channel());
    }
}




void AbstractBridge::set_filter(std::unique_ptr<Filter> &&flt) {
    auto r = _filter.exchange(flt.release());
    flt.reset(r);
}

void AbstractBridge::receive(const ClearPath &cp) {
    _ptr->clear_return_path(this, cp.sender, cp.receiver);
}


void AbstractBridge::on_message(const Message &message, bool pm) noexcept {
    auto flt = _filter.load();
    if (flt) {
        //block message if it is not personal message (response) or not allowed channel
        if (!pm && !flt->on_outgoing(message.get_channel())) return;
    }
    send(message);
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

bool Filter::on_incoming(ChannelID )  {return true;}
bool Filter::on_outgoing(ChannelID) {return true;}
bool Filter::on_incoming_add_to_group(ChannelID, ChannelID) {return true;}
bool Filter::on_outgoing_add_to_group(ChannelID, ChannelID)  {return true;}
bool Filter::on_incoming_close_group(ChannelID)  {return true;}
bool Filter::on_outgoing_close_group(ChannelID) {return true;}

void AbstractBridge::receive(const CloseGroup &msg) {
    auto flt = _filter.load();
    if (flt && !flt->on_incoming_close_group(msg.group)) return;
    _ptr->close_group(this,msg.group);
}

void AbstractBridge::receive(const AddToGroup &msg) {
    auto flt = _filter.load();
    if (flt && !flt->on_incoming_add_to_group(msg.group, msg.target)) {
        ChannelID g = msg.group;
        send(ChannelUpdate{ChannelList(&g,1), Operation::erase});
        return;
    }
    if (!_ptr->add_to_group(this, msg.group, msg.target)) {
        ChannelID g = msg.group;
        send(ChannelUpdate{ChannelList(&g,1), Operation::erase});
        return;
    }
}

void AbstractBridge::on_close_group(ChannelID group_name) noexcept {
    auto flt = _filter.load();
    if (flt && !flt->on_outgoing_close_group(group_name)) return;
    send(CloseGroup{group_name});
}

void AbstractBridge::on_clear_path(ChannelID sender, ChannelID receiver) noexcept {
    send(ClearPath{sender, receiver});
}

void AbstractBridge::on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept {
    auto flt = _filter.load();
    if (flt && !flt->on_outgoing_add_to_group(group_name, target_id)) return;
    send(AddToGroup{group_name, target_id});
}


AbstractBridge::~AbstractBridge() {
    _ptr->unsubscribe_all(this);
    auto flt = _filter.load();
    delete flt;
}

}
