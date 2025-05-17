#include "bridge.hpp"

namespace zerobus {

Bridge::Bridge(Bus bus, std::unique_ptr<AbstractTransport> transport, BridgeOpMode mode)
   :_bus(std::move(bus))
   ,_transport(std::move(transport))
   ,_target(_transport.get())
   ,_op_mode(mode)
    {
        _bus.channel_notify(this, true);
        _transport->set_target(this);
    }

Bridge::Bridge(Bus bus, BridgeOpMode op)
    :_bus(std::move(bus))
    ,_target(nullptr)
    ,_op_mode(op)
{
    _bus.channel_notify(this, true);
}

void Bridge::set_target(IProtocol *target) {
    _target = target;
}

Bridge::~Bridge() {
    _bus.channel_notify(this, false);
    _bus.unsubscribe_all(this);
    if (_transport) _transport->set_target(nullptr);
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
        //perform update
        on_channels_update_lk((n & chan_need_reset) != 0);
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

void Bridge::refresh(bool force) {
    if (force) {
        //request to need reset channels
        _lk_flag.fetch_or(chan_need_reset, std::memory_order_relaxed);
    }
    //perform update
    on_channels_update();

}

void Bridge::on_channels_update_lk(bool force) noexcept {
    auto srl = _bus.get_serial();
    if (srl.serial != _serial_id || force) {
        _serial_id = srl.serial;
        if (srl.source != this) {
            _target->on_message(bmsg::UpdateSerial{_serial_id});
        }
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
    if (force) {
        std::swap(_cur_list, _tmp_list);
        _target->on_message(bmsg::SetChannels{new_lst});
        return;

    }

    ChannelList added = _diff_list.set_difference(new_lst, old_lst);
    if (!added.empty()) {
        _target->on_message(bmsg::AddChannels{added});
    }
    ChannelList removed = _diff_list.set_difference(old_lst, new_lst);
    if (!removed.empty()) {
        _target->on_message(bmsg::EraseChannels{removed});
    }
    std::swap(_cur_list, _tmp_list);
}

void Bridge::on_close_group(ChannelID group_name) noexcept {
    _target->on_message(bmsg::CloseGroup{group_name});
}

void Bridge::on_no_route(ChannelID sender, ChannelID receiver, ConversationID cid) noexcept{
    _target->on_message(bmsg::NoRoute{sender, receiver,cid});
}

void Bridge::on_group_empty(ChannelID group_name) noexcept{
    _target->on_message(bmsg::GroupEmpty{group_name});
}

void Bridge::on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept{
    _target->on_message(bmsg::AddToGroup{group_name, target_id});
}

void Bridge::on_message(const Message &message, bool pm) noexcept{
    if (!pm) {
        _target->on_message(message);
    }
    else _bus.clear_path(message.get_sender(), message.get_channel(), message.get_conversation());
}

void Bridge::on_message(const Message &msg) noexcept{
    if (!_bus.forward_message(this, msg)) {
        _bus.clear_path(msg.get_sender(), msg.get_channel(), msg.get_conversation());
    }
}

void Bridge::on_message(const bmsg::SetChannels &msg) noexcept {

    if (!msg.lst.empty() && _cycle_status.load(std::memory_order_relaxed)) {
        on_message(bmsg::SetChannels{});
        return;
    }

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

void Bridge::on_message(const bmsg::AddChannels &msg) noexcept {
    if (_cycle_status.load(std::memory_order_relaxed)) {
        on_message(bmsg::SetChannels{});
        return;
    }
    _bus.subscribe(this, msg.lst);
}

void Bridge::on_message(const bmsg::EraseChannels &msg) noexcept {
    _bus.unsubscribe(this, msg.lst);
}

void Bridge::on_message(const bmsg::UpdateSerial &msg) noexcept {
    auto st = _bus.update_serial(this,SerialID(msg.serial));
    bool is_cycle = false;
    bool make_reply = false;
    switch (st) {
        default: make_reply = true; break;
        case UpdateSerialStatus::cycle: is_cycle = true; break;
        case UpdateSerialStatus::same:  break;
        case UpdateSerialStatus::changed: break;
    }

    bool pstate = _cycle_status.exchange(is_cycle, std::memory_order_relaxed);
    if (pstate != is_cycle) {
        refresh(true);
        make_reply = false;
    } else if (is_cycle){
        make_reply = false;
    }
    if (make_reply) {
        _target->on_message(bmsg::UpdateSerial{_bus.get_serial().serial});
    }

}

void Bridge::on_message(const bmsg::ChannelReset &) noexcept {
    //request to need reset channels
    _lk_flag.fetch_or(chan_need_reset, std::memory_order_relaxed);
    //perform update
    on_channels_update();
}

void Bridge::on_message(const bmsg::NewSession &) noexcept {
    on_message(bmsg::ChannelReset{});
}

void Bridge::on_message(const bmsg::NoRoute &msg) noexcept {
    _bus.clear_path(msg.sender, msg.receiver, msg.cid);
}

void Bridge::on_message(const bmsg::CloseGroup &msg) noexcept {
    _bus.close_group(this, msg.group);
}

void Bridge::on_message(const bmsg::GroupEmpty &msg) noexcept {
    _bus.unsubscribe(this, msg.group);
}

void Bridge::on_message(const bmsg::AddToGroup &msg) noexcept {
    _bus.add_to_group(this, msg.group, msg.target);
}

void Bridge::send_reset() {
    _target->on_message(bmsg::ChannelReset{});
}

void Bridge::send_new_session(unsigned long version) {
    _target->on_message(bmsg::NewSession{version});
}


}
