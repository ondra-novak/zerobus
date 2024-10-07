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



void DirectBridge::Bridge::send_channels(const ChannelList &channels, Operation op) noexcept {
    auto r = persist_channel_list(channels, _pchns, _pchrs);    //persists this to avoid reference volatile buffers
    _owner.on_update_chanels(*this, r, op);
}


DirectBridge::Bridge& DirectBridge::select_other(const Bridge &other) {
    if (&other == &_b1) return _b2;
    if (&other == &_b2) return _b1;
    throw std::runtime_error("Invalid source bridge instance (unreachable code)");
}

void DirectBridge::on_update_chanels(const Bridge &source, const Bridge::ChannelList &channels, Bridge::Operation op) {
    select_other(source).apply_their_channels(channels,op);
}

void DirectBridge::on_message(const Bridge &source, const Message &msg) {
    select_other(source).dispatch_message(Message(msg));
}

void DirectBridge::Bridge::on_channels_update() noexcept {
    send_mine_channels();
}

void DirectBridge::Bridge::send_message(const Message &msg) noexcept {
    _owner.on_message(*this, msg);
}



void DirectBridge::Bridge::send_reset() noexcept {
    _owner.send_reset(*this);
}

void DirectBridge::Bridge::send_clear_path(ChannelID sender, ChannelID receiver) noexcept {
    _owner.send_clear_path(*this, sender, receiver);
}

void DirectBridge::send_reset(const Bridge &source) {
    select_other(source).apply_their_reset();
}

void DirectBridge::send_clear_path(const Bridge &source, ChannelID sender, ChannelID receiver) {
    select_other(source).apply_their_clear_path(sender, receiver);
}


void DirectBridge::connect() {
    if (!_connected) {
        _connected = true;
        send_reset(_b1);
        send_reset(_b2);
        _b1.on_channels_update();
        _b2.on_channels_update();
    }

}

void DirectBridge::Bridge::cycle_detection(bool state) noexcept {
    _owner.cycle_detection(*this, state);
}

}
