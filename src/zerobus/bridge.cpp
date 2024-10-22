#include "bridge.h"
#include <algorithm>
#include <mutex>
#include <numeric>
#include <iterator>

namespace zerobus {

static std::function<void(AbstractBridge *lsn, bool cycle)> cycle_report;


AbstractBridge::AbstractBridge(Bus bus)
    :_ptr(std::static_pointer_cast<IBridgeAPI>(bus.get_handle())) {}




void AbstractBridge::process_mine_channels(ChannelList lst, bool reset) noexcept {

    auto flt = _filter.load();
    if (flt) {
        auto e = std::remove_if(lst.begin(), lst.end(), [&](const ChannelID ch){
           return !flt->on_incoming(ch); //we block announcing channel to prevent incoming message
        });
        lst = ChannelList(lst.begin(), e);
        check_rules(flt);
    }


    auto srl = _ptr->get_serial(this);
    std::hash<std::string_view> hasher;
    auto h = hasher(srl);
    if (h != _srl_hash) {
        _srl_hash = h;
        if (!srl.empty()) send(UpdateSerial{srl});
    }
    if (_cycle_detected) lst = {};

    if (_cur_channels.empty() || reset) {
        if (!lst.empty()) send(ChannelUpdate{lst, Operation::replace});
    } else if (lst.empty()) {
        send(ChannelUpdate{lst,  Operation::replace});
    } else {
        bool p = false;
        std::set_difference(lst.begin(), lst.end(),
                _cur_channels.begin(), _cur_channels.end(), std::back_inserter(_tmp));
        if (!_tmp.empty()) {send(ChannelUpdate{_tmp,  Operation::add}); p = true;}
        _tmp.clear();

        std::set_difference(_cur_channels.begin(), _cur_channels.end(),
                lst.begin(), lst.end(), std::back_inserter(_tmp));
        if (!_tmp.empty()) {send(ChannelUpdate{_tmp,  Operation::erase}); p = true;}
        _tmp.clear();
        if (!p)
            return;
    }
    persist_channel_list(lst, _cur_channels, _char_buffer);
}

void AbstractBridge::send_mine_channels(bool reset) noexcept {
    constexpr unsigned int reset_flag  = 1 << 10;
    constexpr unsigned int lock_flag = 1;
    bool rep;
    do {
        if (_send_mine_channels_lock.fetch_add(lock_flag + (reset?reset_flag:0)) != 0) return;
        if (!_cycle_detected) {
            process_mine_channels(_ptr->get_active_channels(this, _bus_channels), reset);
        } else {
            process_mine_channels({}, reset);
        }
        auto r = _send_mine_channels_lock.exchange(0);
        auto r1 = r & (reset_flag-1);
        auto r2 = r / (reset_flag);
        rep = r1 > 1;
        reset = r2 > 1;
    }
    //repeat send_mine_channels if requested otherwise unlock
    while (rep);
}

void AbstractBridge::receive(const ChannelUpdate &chan_up) {
    if (_cycle_detected) return;
    ChannelList chans = chan_up.lst;
    if (chan_up.op != Operation::erase) {
        auto flt = _filter.load();
        if (flt) {
            auto e = std::remove_if(chans.begin(), chans.end(), [&](const ChannelID x){
               auto r = flt->on_outgoing(x);
               return !r;
            });
            chans = {chans.begin(), e};
            check_rules(flt);
        }
    }
    _ptr->update_subscribtion(this, chan_up.op, chans);
}

void AbstractBridge::receive(ChannelReset) {
    send_mine_channels(true);
}


void AbstractBridge::receive(const Message &msg) {
    auto ch = msg.get_channel();
    if (_ptr->is_channel(ch)) {
        if (_cycle_detected) return;    //block message to public channel if cycle detected
        auto flt = _filter.load();
        if (flt) {
            auto r = flt->on_incoming(ch);
            check_rules(flt);
            if (!r) {
                //unsubscribe filtered channel
                ChannelID chid = msg.get_channel();
                send(ChannelUpdate{{&chid,1}, Operation::erase});
            }
        }
    }
    if (!_ptr->dispatch_message(this, msg, true)) {
        on_no_route(msg.get_sender(), msg.get_channel()); //report that we unable to process message if no route
    }
}




void AbstractBridge::set_filter(std::unique_ptr<Filter> &flt) {
    auto r = _filter.exchange(flt.release());
    flt.reset(r);
}

void AbstractBridge::set_filter(std::unique_ptr<Filter> &&flt) {
    set_filter(flt);
}

void AbstractBridge::receive(const NoRoute &cp) {
    _ptr->clear_return_path(this, cp.sender, cp.receiver);
}

void AbstractBridge::on_group_empty(ChannelID group_name) noexcept {
    auto flt = _filter.load();
    if (flt) {
        flt->on_incoming_close_group(group_name);
        check_rules(flt);
    }
    send(Msg::GroupEmpty{group_name});
}

void AbstractBridge::receive(const UpdateSerial &msg) {
    bool srl_state = _ptr->set_serial(this, msg.serial);
    if (srl_state == _cycle_detected) {
        _cycle_detected = !_cycle_detected;
        cycle_detection(_cycle_detected);
        send_mine_channels();
        if (_cycle_detected) {
            _ptr->unsubscribe_all_channels(this, false);
        } else {
            send(ChannelReset{});
        }
    }
}

void AbstractBridge::on_message(const Message &message, bool pm) noexcept {
    if (!pm) {
        if (_cycle_detected) return; //block message if cycle detected;
        auto flt = _filter.load();
        if (flt) {
            bool r = flt->on_outgoing(message.get_channel());
            check_rules(flt);
            if (!r) {
                //we cannot pass message to a channel
                //so unsubscribe this channel
                _ptr->unsubscribe(this, message.get_channel());
                return;
            }
        }
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
    if (!flt || flt->on_incoming_close_group(msg.group)) {
        _ptr->close_group(this,msg.group);
    }
    check_rules(flt);
}

void AbstractBridge::receive(const AddToGroup &msg) {
    auto flt = _filter.load();
    if ((flt && !flt->on_incoming_add_to_group(msg.group, msg.target))
    ||  !_ptr->add_to_group(this, msg.group, msg.target)) {
        ChannelID g = msg.group;
        send(ChannelUpdate{ChannelList(&g,1), Operation::erase});
    }
    check_rules(flt);
}

void AbstractBridge::on_close_group(ChannelID group_name) noexcept {
    auto flt = _filter.load();
    if (!flt || flt->on_outgoing_close_group(group_name)) {
        send(CloseGroup{group_name});
    }
    check_rules(flt);
}

void AbstractBridge::on_no_route(ChannelID sender, ChannelID receiver) noexcept {
    send(NoRoute{sender, receiver});
}

void AbstractBridge::on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept {
    auto flt = _filter.load();
    if (!flt || flt->on_outgoing_add_to_group(group_name, target_id)) {
        send(AddToGroup{group_name, target_id});
    } else {
        _ptr->unsubscribe(this, group_name);
    }
    check_rules(flt);
}


AbstractBridge::~AbstractBridge() {
    _ptr->unsubscribe_all(this);
    auto flt = _filter.load();
    delete flt;
}

void AbstractBridge::receive(const GroupEmpty &msg) {
    auto flt = _filter.load();
    if (flt) {
        flt->on_outgoing_close_group(msg.group);
        check_rules(flt);
    }
    _ptr->unsubscribe(this, msg.group);
}
void AbstractBridge::receive(const NewSession &msg) {
    _version = msg.version;
    _ptr->unsubscribe_all_channels(this, true);
    _srl_hash = 0;  //force update_session
    if (_cycle_detected) {
        _cycle_detected = false;    //a new session should break cycle
        cycle_detection(_cycle_detected);
    }

    send_mine_channels(true);
}

void AbstractBridge::cycle_detection(bool cycle) noexcept {
    if (cycle_report) {
        cycle_report(this, cycle);
    }
}

void AbstractBridge::install_cycle_detection_report(std::function<void(AbstractBridge *lsn, bool cycle)> rpt) {
    cycle_report = std::move(rpt);
}

void AbstractBridge::check_rules(Filter *flt) {
    if (flt && flt->commit_rule_changed()) {
        IBus::ChannelListStorage tmp;
        ChannelList chans = _ptr->get_subscribed_channels(this, tmp);
        for (const auto &x: chans) {
            if (!flt->on_outgoing(x))  {
                _ptr->unsubscribe(this, x);
            }
        }
        send_mine_channels(false);
    }
}




}
