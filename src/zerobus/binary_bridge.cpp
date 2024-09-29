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
        case MessageType::channels:
            if (_disabled) return false;
            parse_channels(message);
            return true;
        case MessageType::message:
            if (_disabled) return false;
            parse_message(message);
            return true;
        case MessageType::ping:
            send_pong();
            return true;
        case MessageType::pong:
            return true;
        case MessageType::auth_req:
            parse_auth_req(message);
            return true;
        case MessageType::auth_response:
            parse_auth_resp(message);
            return true;
        case MessageType::welcome:
            peer_reset();
            on_welcome();
            return true;
        default:
            return false;
    }
}

void AbstractBinaryBridge::parse_channels(std::string_view message) {
    std::size_t cnt = read_channel_list_count(message);
    if (cnt <= static_channel_list_buffer) {
        std::string_view buff[static_channel_list_buffer];
        read_channel_list(message, buff);
        apply_their_channels({buff, cnt});
    } else {
        std::vector<std::string_view> buff;
        buff.resize(cnt);
        read_channel_list(message, buff.data());
        apply_their_channels({buff.data(), cnt});
    }
}

void AbstractBinaryBridge::send_channels(const ChannelList &channels) noexcept {
    auto need_space = std::accumulate(channels.begin(), channels.end(),0U, [&](auto a, const auto &x){
        return a + x.size();
    });
    need_space += 9+9*channels.size();
    if (need_space <= static_output_buffer_size) {
        char buff[static_output_buffer_size];
        auto out = write_channel_list(buff, channels);
        output_message(std::string_view(buff,std::distance(buff, out)));
    } else {
        std::vector<char> buff;
        write_channel_list(std::back_inserter(buff), channels);
        output_message(std::string_view(buff.data(), buff.size()));
    }
}

void AbstractBinaryBridge::send_message(const Message &msg) noexcept {
    auto need_space = msg.get_channel().size()+msg.get_content().size()+msg.get_sender().size()+5*9;
    if (need_space <= static_output_buffer_size) {
        char buff[static_output_buffer_size];
        auto out = write_message(buff, msg);
        output_message(std::string_view(buff,std::distance(buff, out)));
    } else {
        std::vector<char> buff;
        write_message(std::back_inserter(buff), msg);
        output_message(std::string_view(buff.data(), buff.size()));
    }

}

void AbstractBinaryBridge::request_auth(std::string_view digest_type) {
    _disabled=true;
    std::random_device rdev;
    std::default_random_engine rnd(rdev());
    std::uniform_int_distribution<unsigned char> dist(0,255);
    for (char &c: _salt) {
        c = dist(rnd);
    }
    std::vector<char> buff;
    buff.push_back(static_cast<char>(MessageType::auth_req));
    write_string(std::back_inserter(buff), digest_type);
    write_string(std::back_inserter(buff), {_salt,sizeof(_salt)});
    output_message({buff.data(), buff.size()});
}

void AbstractBinaryBridge::send_ping() {
    char m = static_cast<char>(MessageType::ping);
    output_message({&m, 1});
}

void AbstractBinaryBridge::send_pong() {
    char m = static_cast<char>(MessageType::pong);
    output_message({&m, 1});
}

void AbstractBinaryBridge::send_welcome() {
    _disabled = true;
    char m = static_cast<char>(MessageType::welcome);
    output_message({&m, 1});
}

void AbstractBinaryBridge::send_auth_response(std::string_view ident, std::string_view proof) {
    std::vector<char> buff;
    buff.push_back(static_cast<char>(MessageType::auth_response));
    write_string(std::back_inserter(buff), ident);
    write_string(std::back_inserter(buff), proof);
    output_message({buff.data(), buff.size()});
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

}
