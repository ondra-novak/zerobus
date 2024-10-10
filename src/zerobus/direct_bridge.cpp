#include "direct_bridge.h"

namespace zerobus {

DirectBridge::Bridge::Bridge(DirectBridge &owner, Bus &&b)
        :AbstractBridge(std::move(b)),_owner(owner) {
    register_monitor(this);
}

DirectBridge::Bridge::~Bridge() {
    unregister_monitor(this);
}

DirectBridge::DirectBridge(Bus b1, Bus b2, bool connect_now)
          :_b1(*this, std::move(b1)), _b2(*this, std::move(b2)) {
   if (connect_now) {
       connect();
   }
}



void DirectBridge::Bridge::send(ChannelUpdate &&msg) noexcept {
    msg.lst = persist_channel_list(msg.lst, _pchns, _pchrs);    //persists this to avoid reference volatile buffers
    _owner.on_send(*this, std::move(msg));
}


DirectBridge::Bridge& DirectBridge::select_other(const Bridge &other) {
    if (&other == &_b1) return _b2;
    if (&other == &_b2) return _b1;
    throw std::runtime_error("Invalid source bridge instance (unreachable code)");
}

void DirectBridge::on_send(const Bridge &source, Bridge::ChannelUpdate &&msg) {
    select_other(source).receive(std::move(msg));
}

void DirectBridge::on_send(const Bridge &source, Message &&msg) {
    select_other(source).receive(std::move(msg));
}

void DirectBridge::Bridge::on_channels_update() noexcept {
    send_mine_channels();
}

void DirectBridge::Bridge::send(Message &&msg) noexcept {
    _owner.on_send(*this, std::move(msg));
}



void DirectBridge::Bridge::send(ChannelReset &&r) noexcept {
    _owner.on_send(*this, std::move(r));
}


void DirectBridge::on_send(const Bridge &source, Bridge::ChannelReset &&r) {
    select_other(source).receive(std::move(r));
}



void DirectBridge::connect() {
    if (!_connected) {
        _connected = true;
        on_send(_b1, Bridge::ChannelReset{});
        on_send(_b1, Bridge::ChannelReset{});
        _b1.on_channels_update();
        _b2.on_channels_update();
    }

}


void DirectBridge::Bridge::cycle_detection(bool state) noexcept {
    _owner.cycle_detection(*this, state);
}

void DirectBridge::Bridge::send(CloseGroup &&msg) noexcept {
    _owner.on_send(*this, std::move(msg));
}

void DirectBridge::Bridge::send(ClearPath &&msg) noexcept {
    _owner.on_send(*this, std::move(msg));
}

void DirectBridge::on_send(const Bridge &source, Bridge::CloseGroup &&g) {
    select_other(source).receive(std::move(g));
}

void DirectBridge::on_send(const Bridge &source,Bridge::ClearPath &&p) {
    select_other(source).receive(std::move(p));
}

void DirectBridge::on_send(const Bridge &source, Bridge::AddToGroup &&g) {
    select_other(source).receive(std::move(g));
}

void DirectBridge::Bridge::send(AddToGroup &&msg) noexcept {
    _owner.on_send(*this, std::move(msg));
}


}
