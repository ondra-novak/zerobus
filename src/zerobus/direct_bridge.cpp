#include "direct_bridge.h"

namespace zerobus {

DirectBridge::Bridge::Bridge(DirectBridge &owner, Bus &&b)
        :AbstractBridge(std::move(b)),_owner(owner) {
    register_monitor(this);
}

DirectBridge::Bridge::~Bridge() {
    unregister_monitor(this);
}

DirectBridge::DirectBridge(Bus b1, Bus b2)
          :_b1(*this, std::move(b1)), _b2(*this, std::move(b2)) {}


void DirectBridge::Bridge::send_channels(const ChannelList &channels) noexcept {
    _owner.on_update_chanels(*this, channels);
}

void DirectBridge::Bridge::on_message(const Message &message,bool ) noexcept {
    _owner.on_message(*this, message);
}

DirectBridge::Bridge& DirectBridge::select_other(const Bridge &other) {
    if (&other == &_b1) return _b2;
    if (&other == &_b2) return _b1;
    throw std::runtime_error("Invalid source bridge instance (unreachable code)");
}

void DirectBridge::on_update_chanels(const Bridge &source, const Bridge::ChannelList &channels) {
    Bridge &target = select_other(source);
    target.apply_their_channels(channels);
}

void DirectBridge::on_message(const Bridge &source, const Message &msg) {
    Bridge &target = select_other(source);
    target.dispatch_message(Message(msg));
}

void DirectBridge::Bridge::on_channels_update() noexcept {
    send_mine_channels();
}

void DirectBridge::Bridge::send_message(const Message &msg) noexcept {
    _owner.on_message(*this, msg);
}

bool DirectBridge::Bridge::on_message_dropped(IListener *, const Message &) noexcept {
    return false;
}

}
