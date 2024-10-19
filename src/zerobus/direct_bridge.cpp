#include "direct_bridge.h"
#include <stdexcept>

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



void DirectBridge::Bridge::send(const ChannelUpdate &msg) noexcept {
    _owner.on_send(*this, msg);
}


DirectBridge::Bridge& DirectBridge::select_other(const Bridge &other) {
    if (&other == &_b1) return _b2;
    if (&other == &_b2) return _b1;
    throw std::runtime_error("Invalid source bridge instance (unreachable code)");
}

void DirectBridge::on_send(const Bridge &source, const Bridge::ChannelUpdate &msg) {
    select_other(source).receive(msg);
}

void DirectBridge::on_send(const Bridge &source, const Message &msg) {
    select_other(source).receive(msg);
}

void DirectBridge::Bridge::on_channels_update() noexcept {
    send_mine_channels();
}

void DirectBridge::Bridge::send(const Message &msg) noexcept {
    _owner.on_send(*this, msg);
}

void DirectBridge::Bridge::send(const NewSession &msg) noexcept {
    _owner.on_send(*this, msg);
}
void DirectBridge::Bridge::send(const UpdateSerial &msg) noexcept {
    _owner.on_send(*this, msg);
}


void DirectBridge::Bridge::send(const ChannelReset &r) noexcept {
    _owner.on_send(*this, r);
}


void DirectBridge::on_send(const Bridge &source, const Bridge::ChannelReset &r) {
    select_other(source).receive(r);
}



void DirectBridge::connect() {
    if (!_connected) {
        _connected = true;
        on_send(_b1, Bridge::NewSession{1});
        on_send(_b2, Bridge::NewSession{1});
        _b1.on_channels_update();
        _b2.on_channels_update();
    }

}


void DirectBridge::Bridge::cycle_detection(bool state) noexcept {
    _owner.cycle_detection(*this, state);
}

void DirectBridge::Bridge::send(const CloseGroup &msg) noexcept {
    _owner.on_send(*this, msg);
}

void DirectBridge::Bridge::send(const NoRoute &msg) noexcept {
    _owner.on_send(*this, msg);
}
void DirectBridge::on_send(const Bridge &source, const Bridge::GroupEmpty &msg) {
    select_other(source).receive(msg);
}

void DirectBridge::on_send(const Bridge &source, const Bridge::NewSession &msg)
{
    select_other(source).receive(msg);
}

void DirectBridge::on_send(const Bridge &source, const Bridge::CloseGroup &g) {
    select_other(source).receive(g);
}

void DirectBridge::on_send(const Bridge &source,const Bridge::NoRoute &p) {
    select_other(source).receive(p);
}
void DirectBridge::on_send(const Bridge &source,const Bridge::UpdateSerial &p) {
    select_other(source).receive(p);
}

AbstractBridge& DirectBridge::getBridge1() {
    return _b1;
}

const AbstractBridge& DirectBridge::getBridge1() const {
    return _b1;
}

AbstractBridge& DirectBridge::getBridge2() {
    return _b2;
}

const AbstractBridge& DirectBridge::getBridge2() const {
    return _b2;
}

void DirectBridge::on_send(const Bridge &source, const Bridge::AddToGroup &g) {
    select_other(source).receive(g);
}

void DirectBridge::Bridge::send(const AddToGroup &msg) noexcept {
    _owner.on_send(*this, msg);
}

void DirectBridge::Bridge::send(const AbstractBridge::GroupEmpty& msg) noexcept {
    _owner.on_send(*this,  msg);
}


}

