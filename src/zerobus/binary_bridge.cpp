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

void AbstractBinaryBridge::parse_channels(std::string_view message, Operation op) {
    std::size_t cnt = read_channel_list_count(message);
    if (cnt <= static_channel_list_buffer) {
        std::string_view buff[static_channel_list_buffer];
        read_channel_list(message, buff);
        apply_their_channels({buff, cnt}, op);
    } else {
        std::vector<std::string_view> buff;
        buff.resize(cnt);
        read_channel_list(message, buff.data());
        apply_their_channels({buff.data(), cnt}, op);
    }
}

void AbstractBinaryBridge::send_channels(const ChannelList &channels, Operation op) noexcept {
    auto need_space = std::accumulate(channels.begin(), channels.end(),0U, [&](auto a, const auto &x){
        return a + x.size();
    });
    need_space += 9+9*channels.size();
    output_message_helper(need_space, [&](auto iter){
        return write_channel_list(iter, op, channels);
    });
}

void AbstractBinaryBridge::send_message(const Message &msg) noexcept {
    auto need_space = msg.get_channel().size()+msg.get_content().size()+msg.get_sender().size()+5*9;
    output_message_helper(need_space, [&](auto iter){
        return write_message(iter, msg);
    });

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
    char m = static_cast<char>(MessageType::welcome);
    output_message({&m, 1});
}

void AbstractBinaryBridge::send_auth_response(std::string_view ident, std::string_view proof) {
    output_message_helper(20+ident.size()+proof.size(), [&](auto iter){
        *iter++ = static_cast<char>(MessageType::auth_response);
        iter = write_string(iter, ident);
        iter = write_string(iter, proof);
        return iter;
    });
}

void AbstractBinaryBridge::parse_message(std::string_view message) {
    auto msg = read_message([&](auto sender, auto channel, auto msg, auto cid){
        return _ptr->create_message(sender, channel, msg, cid);
    }, message);
    AbstractBridge::dispatch_message(msg);
}

void AbstractBinaryBridge::send_auth_failed() {
    char m = static_cast<char>(MessageType::auth_failed);
    output_message({&m, 1});
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

}

void AbstractBinaryBridge::send_clear_path(zerobus::ChannelID sender, zerobus::ChannelID receiver) noexcept {
    output_message_helper(sender.size()+receiver.size()+20, [&](auto iter){
        *iter++ = static_cast<char>(MessageType::clear_path);
        iter = write_string(iter, sender);
        iter = write_string(iter, receiver);
        return iter;
    });
}

void AbstractBinaryBridge::parse_clear_path(std::string_view message) {
    std::string_view sender = read_string(message);
    std::string_view receiver = read_string(message);
    apply_their_clear_path(sender, receiver);

}

}
