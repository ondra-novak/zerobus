#pragma once

#include "message.hpp"

namespace zerobus {

class IProtocol {
public:

    /// @brief Represents a message to set a list of channels.
    /// This message requires unsubscribing from all current channels and subscribing to the new list.
    struct MsgSetChannels {
        ChannelList lst; ///< List of channels to set.
    };

    /// @brief Represents a message to add a list of channels.
    struct MsgAddChannels {
        ChannelList lst; ///< List of channels to add.
    };

    /// @brief Represents a message to erase a list of channels.
    struct MsgEraseChannels {
        ChannelList lst; ///< List of channels to erase.
    };

    /// @brief Represents a message to update the serial identifier.
    struct MsgUpdateSerial {
        std::string_view serial; ///< The new serial identifier.
    };

    /// @brief This message is sent from the other side to indicate that all channels
    /// have been unsubscribed due to some reason. It requests the current channel
    /// list to be resent.
    struct MsgChannelReset {};

    /// @brief Represents a message to start a new session.
    struct MsgNewSession {
        unsigned long version = 1; ///< Version of the new session.
    };

    /// @brief Represents a message indicating no route exists between sender and receiver.
    struct MsgNoRoute {
        ChannelID sender;   ///< The sender channel ID.
        ChannelID receiver; ///< The receiver channel ID.
    };

    /// @brief Represents a message to close a specific group.
    struct MsgCloseGroup {
        ChannelID group; ///< The group ID to close.
    };

    /// @brief Represents a message indicating a group is empty and has been closed
    struct MsgGroupEmpty {
        ChannelID group; ///< The empty group ID.
    };

    /// @brief Represents a message to add a target channel to a group.
    struct MsgAddToGroup {
        ChannelID group;  ///< The group ID.
        ChannelID target; ///< The target channel ID to add to the group.
    };


    virtual void on_message(const Message &msg) noexcept = 0;
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
