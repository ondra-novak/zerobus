#include "bridge.hpp"

#include <algorithm>
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

std::atomic<int> _cntr_add = 0;

void Bridge::on_channels_update_lk(bool force) noexcept {
    auto srl = _bus.get_serial();
    if (srl.serial != _serial_id || force) {
        _serial_id = srl.serial;
        if (srl.source != this) {
            _target->receive(bmsg::UpdateSerial{_serial_id});
        }
    }

    ChannelList new_lst;

    auto mode = _op_mode.load(std::memory_order_relaxed);


    //if cycle detected, do not propagate channels to other side
    if (_cycle_status.load(std::memory_order_relaxed) == false
            && (mode == BridgeOpMode::bidirectional|| mode == BridgeOpMode::inbound)) {
        new_lst = _bus.get_public_channels(this,_tmp_list);
        std::sort(new_lst.begin(), new_lst.end());
    }

    ChannelList old_lst;
    if (!force) {
        old_lst = _cur_list.get_stored();
    }

    _diff_list.clear();
    std::set_difference(new_lst.begin(), new_lst.end(),
                        old_lst.begin(),old_lst.end(), std::back_inserter(_diff_list));

    if (!_diff_list.empty()) {
        _target->receive(bmsg::AddChannels{ChannelList(_diff_list)});
    }
    _diff_list.clear();
    std::set_difference(old_lst.begin(), old_lst.end(),
                        new_lst.begin(),new_lst.end(), std::back_inserter(_diff_list));
    if (!_diff_list.empty()) {
        _target->receive(bmsg::EraseChannels{ChannelList(_diff_list)});
    }

    std::swap(_cur_list, _tmp_list);
}

void Bridge::on_close_group(ChannelID group_name) noexcept {
    _target->receive(bmsg::CloseGroup{group_name});
}

void Bridge::on_delivery_error(const Undelivered &msg) noexcept{
    _target->receive(msg);
}

void Bridge::on_group_empty(ChannelID group_name) noexcept{
    _target->receive(bmsg::GroupEmpty{group_name});
}

void Bridge::on_add_to_group(ChannelID group_name, ChannelID target_id, ConversationID cid) noexcept{
    _target->receive(bmsg::AddToGroup{group_name, target_id, cid});
}

void Bridge::on_message(const Message &message) noexcept{
    _target->receive(message);
}

void Bridge::on_direct_message(const Message &message) noexcept {
    _bus.delivery_error(Undelivered{
        message.get_sender(), message.get_channel(),
       message.get_conversation(), DeliveryError::invalid_target, message.importance});
}

void Bridge::receive(const Message &msg) noexcept{
    if (!_bus.forward_message(this, msg)) {
        _bus.delivery_error(Undelivered{
            msg.get_sender(), msg.get_channel(),
            msg.get_conversation(), DeliveryError::no_route, msg.importance});
    }
}


void Bridge::receive(const bmsg::AddChannels &msg) noexcept {
    if (_cycle_status.load(std::memory_order_relaxed)) {
        return;
    }
    _bus.subscribe(this, msg.lst);
}

void Bridge::receive(const bmsg::EraseChannels &msg) noexcept {
    _bus.unsubscribe(this, msg.lst);
}

void Bridge::receive(const bmsg::UpdateSerial &msg) noexcept {
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
        _target->receive(bmsg::UpdateSerial{_bus.get_serial().serial});
    }

}

void Bridge::receive(const bmsg::ChannelReset &) noexcept {

    //request to need reset channels
    _lk_flag.fetch_or(chan_need_reset, std::memory_order_relaxed);
    //perform update
    on_channels_update();
}

void Bridge::receive(const bmsg::NewSession &) noexcept {
    receive(bmsg::ChannelReset{});
}

void Bridge::receive(const Undelivered &msg) noexcept {
    _bus.delivery_error(msg);
}

void Bridge::receive(const bmsg::CloseGroup &msg) noexcept {
    _bus.close_group(this, msg.group);
}

void Bridge::receive(const bmsg::GroupEmpty &msg) noexcept {
    _bus.unsubscribe(this, msg.group);
}

void Bridge::receive(const bmsg::AddToGroup &msg) noexcept {
    if (!_bus.add_to_group(this, msg.group, msg.target, msg.cid)) {
        DeliveryError err = DeliveryError::no_route;
        if (!_bus.is_group(this, msg.group)) {
            _target->receive(bmsg::GroupEmpty{msg.group});
        } else {
            auto type = _bus.get_channel_type(msg.group);
            if (type != ChannelType::not_used) {
                err = DeliveryError::name_collision;
            }
        }
        _target->receive(Undelivered{msg.group, msg.target, msg.cid, err, Importance::normal});
    }
}

void Bridge::send_reset() {
    _target->receive(bmsg::ChannelReset{});
}

void Bridge::send_new_session(unsigned long version) {
    _target->receive(bmsg::NewSession{version});
}

void Bridge::receive(const bmsg::Announce&a) noexcept {
    _bus.announce(this, a.request_id, a.sender);
}

void Bridge::on_announce(IListener *sender, ConversationID reqid, ChannelID chan) noexcept {
    if (sender != this) {
        _target->receive(bmsg::Announce{chan, reqid});
    }
}

void Bridge::disconnect() {
    _bus.unsubscribe_all(this);
}
}
