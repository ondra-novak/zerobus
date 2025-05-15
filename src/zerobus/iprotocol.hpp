#pragma once

#include "message.hpp"
#include <variant>
#include <tuple>

namespace zerobus {


template<typename Variant>
struct VariantToTuple;

template<typename... Types>
struct VariantToTuple<std::variant<Types...>> {
    using type = std::tuple<Types...>;
};

template<typename VariantType>
using variant_to_tuple = typename VariantToTuple<VariantType>::type;

template<int ID, typename Tuple>
struct FindById;

template<int ID, typename First, typename... Rest>
struct FindById<ID, std::tuple<First, Rest...>> {
    using type = std::conditional_t<
        (First::id == ID),
        First,
        typename FindById<ID, std::tuple<Rest...>>::type
    >;
};

template<int ID>
struct FindById<ID, std::tuple<>> {
    static_assert(ID == -1, "Typ s tímto ID nebyl nalezen.");
};

template<int ID, typename Tuple>
using find_by_id = typename FindById<ID, Tuple>::type;

class IProtocol {
public:

    struct MsgMessage {
        static constexpr int uid = 4;
        ChannelID sender;
        ChannelID channel;
        MessageContent content;
        ConversationID cid;
    };

    struct MsgChannelsBase {
        ChannelList lst; ///< List of channels to set.
    };
    /// @brief Represents a message to set a list of channels.
    /// This message requires unsubscribing from all current channels and subscribing to the new list.
    struct MsgSetChannels: MsgChannelsBase {
        static constexpr int uid = 1;

    };

    /// @brief Represents a message to add a list of channels.
    struct MsgAddChannels: MsgChannelsBase {
        static constexpr int uid = 2;
    };

    /// @brief Represents a message to erase a list of channels.
    struct MsgEraseChannels: MsgChannelsBase {
        static constexpr int uid = 3;
    };

    /// @brief Represents a message to update the serial identifier.
    struct MsgUpdateSerial {
        static constexpr int uid = 5;
        std::string_view serial; ///< The new serial identifier.
    };

    /// @brief This message is sent from the other side to indicate that all channels
    /// have been unsubscribed due to some reason. It requests the current channel
    /// list to be resent.
    struct MsgChannelReset {
        static constexpr int uid = 0;
    };

    /// @brief Represents a message to start a new session.
    struct MsgNewSession {
        static constexpr int uid = 6;
        unsigned long version = 1; ///< Version of the new session.
    };

    /// @brief Represents a message indicating no route exists between sender and receiver.
    struct MsgNoRoute {
        static constexpr int uid = 10;
        ChannelID sender;   ///< The sender channel ID.
        ChannelID receiver; ///< The receiver channel ID.
        ConversationID cid;
    };

    /// @brief Represents a message to close a specific group.
    struct MsgCloseGroup {
        static constexpr int uid = 8;
        ChannelID group; ///< The group ID to close.
    };

    /// @brief Represents a message indicating a group is empty and has been closed
    struct MsgGroupEmpty {
        static constexpr int uid = 9;
        ChannelID group; ///< The empty group ID.
    };

    /// @brief Represents a message to add a target channel to a group.
    struct MsgAddToGroup {
        static constexpr int uid = 7;
        ChannelID group;  ///< The group ID.
        ChannelID target; ///< The target channel ID to add to the group.
    };


    virtual void on_message(const MsgMessage &msg) noexcept = 0;
    virtual void on_message(const MsgSetChannels &msg) noexcept = 0;
    virtual void on_message(const MsgAddChannels &msg) noexcept = 0;
    virtual void on_message(const MsgEraseChannels &msg) noexcept = 0;
    virtual void on_message(const MsgUpdateSerial &msg) noexcept = 0;
    virtual void on_message(const MsgChannelReset &msg) noexcept = 0;
    virtual void on_message(const MsgNewSession &msg) noexcept = 0;
    virtual void on_message(const MsgNoRoute &msg) noexcept = 0;
    virtual void on_message(const MsgCloseGroup &msg) noexcept = 0;
    virtual void on_message(const MsgGroupEmpty &msg) noexcept = 0;
    virtual void on_message(const MsgAddToGroup &msg) noexcept = 0;
    virtual ~IProtocol() = default;
};

}
