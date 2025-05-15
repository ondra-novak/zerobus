#include "bridge.hpp"

namespace zerobus {

Bridge::Bridge(Bus bus, std::shared_ptr<AbstractTransport> transport, BridgeOpMode mode)
   :_bus(std::move(bus))
   ,_transport(std::move(transport))
   ,_op_mode(mode)
    {
        _bus.channel_notify(this, true);
        _transport->set_target(this);
    }


Bridge::~Bridge() {
    _bus.channel_notify(this, false);
    _bus.unsubscribe_all(this);
    _transport->set_target(nullptr);
}

void Bridge::on_channels_update() noexcept {
    //lock and read requests
    auto n = _lk_flag.exchange(chan_locked);
    do {
        if (n & chan_locked) {   //already locked?
            n |= chan_need_update;  //add flag that we need update
            n = _lk_flag.exchange(n); //try to update flag
            if (n & chan_locked) return; //if still locked, we done
        }
        //locked for us
        //if reset requested, do reset
        if (n & chan_need_reset) {
            _cur_list.store_channels({});
        }
        //perform update
        on_channels_update_lk();
        //unlock (set zero), read requests
        n = _lk_flag.exchange(0);
        //there should be no requests exit
        if (n == chan_locked) return;
        //if there are requests, lock it back and repeat
        n = _lk_flag.exchange(chan_locked);
    } while (true);

}

void Bridge::set_mode(BridgeOpMode mode) {
    auto m = _op_mode.exchange(mode, std::memory_order_relaxed);
    if (m != mode) {
        on_channels_update();
    }
}

BridgeOpMode Bridge::get_mode() const {
    return _op_mode.load(std::memory_order_relaxed);
}

void Bridge::on_channels_update_lk() noexcept {
    auto srl = _bus.get_serial();
    if (srl != _serial_id) {
        _serial_id = srl;
        _transport->on_message(MsgUpdateSerial{srl});
    }

    ChannelList new_lst;

    auto mode = _op_mode.load(std::memory_order_relaxed);


    //if cycle detected, do not propagate channels to other side
    if (_cycle_status.load(std::memory_order_relaxed) == false
            && (mode == BridgeOpMode::bidirectional|| mode == BridgeOpMode::inbound)) {
        _bus.get_public_channels(this,_tmp_list);
        new_lst = _tmp_list.make_ordered();
    }

    ChannelList old_lst = _cur_list.get_stored();
    if (_cur_list.get_stored().empty()) {
        std::swap(_cur_list, _tmp_list);
        _transport->on_message(MsgSetChannels{new_lst});
        return;
    }
    ChannelList added = _diff_list.set_difference(old_lst, new_lst);
    if (!added.empty()) {
        _transport->on_message(MsgAddChannels{added});
    }
    ChannelList removed = _diff_list.set_difference(new_lst, old_lst);
    if (!removed.empty()) {
        _transport->on_message(MsgEraseChannels{removed});
    }
    std::swap(_cur_list, _tmp_list);
}

void Bridge::on_close_group(ChannelID group_name) noexcept {
    _transport->on_message(MsgCloseGroup{group_name});
}

void Bridge::on_no_route(ChannelID sender, ChannelID receiver) noexcept{
    _transport->on_message(MsgNoRoute{sender, receiver});
}

void Bridge::on_group_empty(ChannelID group_name) noexcept{
    _transport->on_message(MsgGroupEmpty{group_name});
}

void Bridge::on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept{
    _transport->on_message(MsgAddToGroup{group_name, target_id});
}

void Bridge::on_message(const Message &message, bool pm) noexcept{
    if (!pm) _transport->on_message(message);
    else _bus.clear_path(message.get_sender(), message.get_channel());
}

void Bridge::on_message(const Message &msg) noexcept{
    if (!_bus.forward_message(this, msg)) {
        _bus.clear_path(msg.get_sender(), msg.get_channel());
    }
}

void Bridge::on_message(const MsgSetChannels &msg) noexcept {

    auto m = _op_mode.load(std::memory_order_relaxed);

    ChannelListStorage tmp_list;
    ChannelListStorage diff_list;
    ChannelList cur_lst = _bus.get_subscribed_channels(this,tmp_list);
    cur_lst = tmp_list.make_ordered();
    ChannelList new_lst;
    if (m==BridgeOpMode::outbound || m == BridgeOpMode::bidirectional) {
        new_lst = msg.lst;
    }

    ChannelList added = diff_list.set_difference(cur_lst, new_lst);
    if (!added.empty()) {
        _bus.subscribe(this, added);
    }
    ChannelList removed = diff_list.set_difference(new_lst, cur_lst);
    if (!removed.empty()) {
        _bus.unsubscribe(this, added);
    }
}

void Bridge::on_message(const MsgAddChannels &msg) noexcept {
    _bus.subscribe(this, msg.lst);
}

void Bridge::on_message(const MsgEraseChannels &msg) noexcept {
    _bus.unsubscribe(this, msg.lst);
}

void Bridge::on_message(const MsgUpdateSerial &msg) noexcept {
    bool has_cycle = !_bus.update_serial(this, msg.serial);
    bool pstate = _cycle_status.exchange(has_cycle, std::memory_order_relaxed);
    if (pstate != has_cycle) {
        on_channels_update();
    }

}

void Bridge::on_message(const MsgChannelReset &) noexcept {
    //request to need reset channels
    _lk_flag.fetch_or(chan_need_reset);
    //perform update
    on_channels_update_lk();
}

void Bridge::on_message(const MsgNewSession &) noexcept {
    on_message(MsgChannelReset{});
}

void Bridge::on_message(const MsgNoRoute &msg) noexcept {
    _bus.clear_path(msg.sender, msg.receiver);
}

void Bridge::on_message(const MsgCloseGroup &msg) noexcept {
    _bus.close_group(this, msg.group);
}

void Bridge::on_message(const MsgGroupEmpty &msg) noexcept {
    _bus.unsubscribe(this, msg.group);
}

void Bridge::on_message(const MsgAddToGroup &msg) noexcept {
    _bus.add_to_group(this, msg.group, msg.target);
}

void Bridge::send_reset() {
    _transport->on_message(MsgChannelReset{});
}

void Bridge::send_new_session(unsigned long version) {
    _transport->on_message(MsgNewSession{version});
}


}
