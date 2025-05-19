#include "bus.hpp"
#include "local_bus.hpp"

namespace zerobus {

Bus::Bus(std::shared_ptr<LocalBus> handle):_ptr(std::move(handle)) {}

bool Bus::subscribe(IListener *listener, ChannelID channel) {
    return _ptr->subscribe(listener, {&channel,1});
}

bool Bus::subscribe(IListener *listener, ChannelList channel) {
    return _ptr->subscribe(listener, channel);
}

void Bus::unsubscribe(IListener *listener, ChannelID channel) {
    _ptr->unsubscribe(listener, {&channel,1});
}

void Bus::unsubscribe(IListener *listener, ChannelList channel) {
    _ptr->unsubscribe(listener, channel);
}

void Bus::unsubscribe_all(IListener *listener) {
    _ptr->unsubscribe_all(listener);
}

void Bus::close_private_channel(IListener *listener) {
    _ptr->close_private_channel(listener);
}

bool Bus::add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) {
    return _ptr->add_to_group(owner, group_name, uid);
}

void Bus::close_group(IListener *owner, ChannelID group_name) {
    _ptr->close_group(owner, group_name);
}

void Bus::close_all_groups(IListener *owner) {
    _ptr->close_all_groups(owner);
}

bool Bus::send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) {
    return _ptr->send_message(listener, channel, msg, cid);
}

bool Bus::forward_message(IListener *sender, const Message &msg) {
    return _ptr->forward_message(sender, msg);
}

std::string Bus::get_random_channel_name(std::string_view prefix) const {
    return _ptr->get_random_channel_name(prefix);
}

bool Bus::is_channel(ChannelID id) const {
    return _ptr->is_channel(id);
}

ChannelList Bus::get_subscribed_channels(IListener *listener, ChannelListStorage &storage) const {
    return _ptr->get_subscribed_channels(listener, storage);
}

ChannelList Bus::get_subscribed_groups(IListener *listener, ChannelListStorage &storage) const {
    return _ptr->get_subscribed_groups(listener, storage);
}

void Bus::channel_notify(IChannelNotifyListener *listener, bool enable) {
    _ptr->channel_notify(listener, enable);
}

ChannelList Bus::get_public_channels(IListener *skip_listener, ChannelListStorage &storage) const {
    return _ptr->get_public_channels(skip_listener, storage);
}

void Bus::clear_path(ChannelID sender, ChannelID receiver, ConversationID cid) {
     _ptr->clear_path( sender, receiver, cid);
}

SerialStatus Bus::get_serial() const {
    return _ptr->get_serial();
}

void Bus::announce(IListener *lsn, ConversationID reqid, ChannelID chan) {
    _ptr->announce(lsn, reqid, chan);
}

UpdateSerialStatus Bus::update_serial(IListener *lsn, const SerialID &serialId) {
    return _ptr->update_serial(lsn, serialId);
}


}
