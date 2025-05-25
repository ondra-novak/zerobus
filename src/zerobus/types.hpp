#pragma once
#include <cstdint>
#include <string_view>
#include <span>


namespace zerobus {


using ChannelID = std::string_view;
///messages are string
using MessageContent = std::string_view;
///conversation id - using number is enough
using ConversationID = std::uint32_t;

using ChannelList = std::span<ChannelID>;
using ChannelListMutable = std::span<ChannelID>;


}
