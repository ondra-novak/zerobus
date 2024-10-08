#include "binary_bridge.h"
#include <numeric>
#include <random>

namespace zerobus {

AbstractBinaryBridge::AbstractBinaryBridge(Bus bus)
        :AbstractBridge(std::move(bus)) {}


bool AbstractBinaryBridge::dispatch_message(std::string_view message) {
    if (message.empty()) return false;
    MessageType t = static_cast<MessageType>(message[0]);
    message = message.substr(1);
    switch (t) {
        case MessageType::channels_replace:
            if (_disabled) return false;
            parse_channels(message, Operation::replace);
            return true;
        case MessageType::channels_add:
            if (_disabled) return false;
            parse_channels(message, Operation::add);
            return true;
        case MessageType::channels_erase:
            if (_disabled) return false;
            parse_channels(message, Operation::erase);
            return true;
        case MessageType::message:
            if (_disabled) return false;
            parse_message(message);
            return true;
        case MessageType::add_to_group:
            parse_add_to_group(message);
            return true;
        case MessageType::close_group:
            parse_close_group(message);
            return true;
        case MessageType::auth_failed:
            return true;
        case MessageType::auth_req:
            parse_auth_req(message);
            return true;
        case MessageType::auth_response:
            parse_auth_resp(message);
            return true;
        case MessageType::welcome:
            apply_their_reset();
            on_welcome();
            return true;
        case MessageType::channels_reset:
            apply_their_reset();
            return true;
        case MessageType::clear_path:
            parse_clear_path(message);
            return true;
        default:
            return false;
    }
}

void AbstractBinaryBridge::parse_channels(std::string_view msgtext, Operation op) {
    std::size_t cnt = read_uint(msgtext);
    ChannelID *lst = reinterpret_cast<ChannelID *>(alloca(cnt * sizeof(ChannelID)));
    for (std::size_t i = 0; i < cnt; ++i) {
        std::construct_at(lst+i, read_string(msgtext));
    }
    apply_their_channels({lst, cnt}, op);
    for (std::size_t i = 0; i < cnt; ++i) {
            std::destroy_at(lst+i);
    }
}

void AbstractBinaryBridge::send_channels(const ChannelList &channels, Operation op) noexcept {
    MessageType t;
    switch (op) {
        case Operation::add: t  =MessageType::channels_add;break;
        case Operation::erase: t  =MessageType::channels_erase;break;
        case Operation::replace: t  =MessageType::channels_replace;break;
        default: return;
    }

    auto need_space = std::accumulate(channels.begin(), channels.end(),
            calc_required_space(t, channels.size()), [&](auto a, const auto &x){
        return a + calc_required_space(x);
    });
    output_message_helper(need_space, [&](auto iter){
        *iter++ = static_cast<char>(t);
        iter = write_uint(iter, channels.size());
        for (const ChannelID &ch :channels) iter = write_string(iter, ch);
        return iter;
    });
}

void AbstractBinaryBridge::send_message(const Message &msg) noexcept {
    output_message_fixed_len(MessageType::message, msg.get_conversation(), msg.get_sender(), msg.get_channel(), msg.get_content());

}

void AbstractBinaryBridge::request_auth(std::string_view digest_type) {
    _disabled=true;
    std::random_device rdev;
    std::default_random_engine rnd(rdev());
    std::uniform_int_distribution<unsigned char> dist(0,255);
    for (char &c: _salt) {
        c = dist(rnd);
    }
    auto needsz = digest_type.size() + sizeof(_salt)+1;
    output_message_helper(needsz, [&](auto iter){
        *iter++ = static_cast<char>(MessageType::auth_req);
        iter = std::copy(std::begin(_salt), std::end(_salt), iter);
        iter = std::copy(digest_type.begin(), digest_type.end(), iter);
        return iter;
    });
}


void AbstractBinaryBridge::send_welcome() {
    _disabled = false;
    output_message_fixed_len(MessageType::welcome);
}

void AbstractBinaryBridge::send_auth_response(std::string_view ident, std::string_view proof) {
    output_message_fixed_len(MessageType::auth_response, ident, proof);
}

void AbstractBinaryBridge::parse_message(std::string_view msgtext) {
    ConversationID cid = read_uint(msgtext);
    std::string_view sender = read_string(msgtext);
    std::string_view channel = read_string(msgtext);
    std::string_view content = read_string(msgtext);
    auto msg = _ptr->create_message(sender, channel, content, cid);
    AbstractBridge::dispatch_message(msg);
}

void AbstractBinaryBridge::send_auth_failed() {
    output_message_fixed_len(MessageType::auth_failed);
}

void AbstractBinaryBridge::parse_auth_req(std::string_view message) {
    std::string_view proof_type = message.substr(sizeof(_salt));
    std::string_view salt = message.substr(0, sizeof(_salt));
    on_auth_request(proof_type, salt);
}


void AbstractBinaryBridge::parse_auth_resp(std::string_view message) {
    std::string_view ident = read_string(message);
    std::string_view proof = read_string(message);
    on_auth_response(ident, proof, std::string_view(_salt, sizeof(_salt)));

}

void AbstractBinaryBridge::send_reset() noexcept {
    output_message_fixed_len(MessageType::channels_reset);

}

void AbstractBinaryBridge::on_close_group(ChannelID group_name) noexcept {
    output_message_fixed_len(MessageType::close_group, group_name);
}

void AbstractBinaryBridge::on_clear_path(ChannelID sender, ChannelID receiver) noexcept {
    output_message_fixed_len(MessageType::clear_path, sender, receiver);
}

void AbstractBinaryBridge::on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept {
    output_message_fixed_len(MessageType::add_to_group, group_name, target_id);
}

void AbstractBinaryBridge::parse_clear_path(std::string_view message) {
    std::string_view sender = read_string(message);
    std::string_view receiver = read_string(message);
    apply_their_clear_path(sender, receiver);

}

void AbstractBinaryBridge::parse_add_to_group(std::string_view message) {
    std::string_view group_name = read_string(message);
    std::string_view target_id = read_string(message);
    apply_their_add_to_group(group_name, target_id);
}

void AbstractBinaryBridge::parse_close_group(std::string_view message) {
    std::string_view group_name = read_string(message);
    apply_their_close_group(group_name);
}

}
