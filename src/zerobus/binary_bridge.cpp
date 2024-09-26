#include "binary_bridge.h"
#include <numeric>

namespace zerobus {

BinaryBridge::BinaryBridge(Bus bus,std::function<void(std::string_view)> output_fn)
        :AbstractBridge(std::move(bus))
         ,_output_fn(std::move(output_fn)) {}


bool BinaryBridge::dispatch_message(std::string_view message) {
    if (message.empty()) return false;
    MessageType t = static_cast<MessageType>(message[0]);
    message = message.substr(1);
    switch (t) {
        case MessageType::channels:
            parse_channels(message);
            return true;
        case MessageType::message:
            parse_message(message);
            return true;
        default:
            return false;
    }
}

void BinaryBridge::parse_channels(std::string_view message) {
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

void BinaryBridge::send_channels(const ChannelList &channels) noexcept {
    auto need_space = std::accumulate(channels.begin(), channels.end(),0U, [&](auto a, const auto &x){
        return a + x.size();
    });
    need_space += 9+9*channels.size();
    if (need_space <= static_output_buffer_size) {
        char buff[static_output_buffer_size];
        auto out = write_channel_list(buff, channels);
        _output_fn(std::string_view(buff,std::distance(buff, out)));
    } else {
        std::vector<char> buff;
        write_channel_list(std::back_inserter(buff), channels);
        _output_fn(std::string_view(buff.data(), buff.size()));
    }
}

void BinaryBridge::send_message(const Message &msg) noexcept {
    auto need_space = msg.get_channel().size()+msg.get_content().size()+msg.get_sender().size()+5*9;
    if (need_space <= static_output_buffer_size) {
        char buff[static_output_buffer_size];
        auto out = write_message(buff, msg);
        _output_fn(std::string_view(buff,std::distance(buff, out)));
    } else {
        std::vector<char> buff;
        write_message(std::back_inserter(buff), msg);
        _output_fn(std::string_view(buff.data(), buff.size()));
    }

}

void BinaryBridge::parse_message(std::string_view message) {
    auto msg = read_message([&](auto sender, auto channel, auto msg, auto cid){
        return _ptr->create_message(sender, channel, msg, cid);
    }, message);
    AbstractBridge::dispatch_message(msg);
}

}
