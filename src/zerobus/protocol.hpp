#pragma once

#include "message.hpp"
#include <variant>
#include <tuple>

namespace zerobus {


namespace bmsg {


struct ChannelsBase {
    ChannelList lst; ///< List of channels to set.
};
/// @brief Represents a message to set a list of channels.
/// This message requires unsubscribing from all current channels and subscribing to the new list.
struct SetChannels: ChannelsBase {

};

/// @brief Represents a message to add a list of channels.
struct AddChannels: ChannelsBase {
};

/// @brief Represents a message to erase a list of channels.
struct EraseChannels: ChannelsBase {
};

/// @brief Represents a message to update the serial identifier.
struct UpdateSerial {
    std::string_view serial; ///< The new serial identifier.
};

/// @brief This message is sent from the other side to indicate that all channels
/// have been unsubscribed due to some reason. It requests the current channel
/// list to be resent.
struct ChannelReset {
};

/// @brief Represents a message to start a new session.
struct NewSession {
    unsigned long version = 1; ///< Version of the new session.
};

/// @brief Represents a message indicating no route exists between sender and receiver.
struct NoRoute {
    ChannelID sender;   ///< The sender channel ID.
    ChannelID receiver; ///< The receiver channel ID.
    ConversationID cid;
};

/// @brief Represents a message to close a specific group.
struct CloseGroup {
    ChannelID group; ///< The group ID to close.
};

/// @brief Represents a message indicating a group is empty and has been closed
struct GroupEmpty {
    ChannelID group; ///< The empty group ID.
};

/// @brief Represents a message to add a target channel to a group.
struct AddToGroup {
    ChannelID group;  ///< The group ID.
    ChannelID target; ///< The target channel ID to add to the group.
};

}

class IProtocol {
public:



    virtual void on_message(const Message &) noexcept = 0;
    virtual void on_message(const bmsg::SetChannels &) noexcept = 0;
    virtual void on_message(const bmsg::AddChannels &) noexcept = 0;
    virtual void on_message(const bmsg::EraseChannels &) noexcept = 0;
    virtual void on_message(const bmsg::UpdateSerial &) noexcept = 0;
    virtual void on_message(const bmsg::ChannelReset &) noexcept = 0;
    virtual void on_message(const bmsg::NewSession &) noexcept = 0;
    virtual void on_message(const bmsg::NoRoute &) noexcept = 0;
    virtual void on_message(const bmsg::CloseGroup &) noexcept = 0;
    virtual void on_message(const bmsg::GroupEmpty &) noexcept = 0;
    virtual void on_message(const bmsg::AddToGroup &) noexcept = 0;
    virtual ~IProtocol() = default;
};

class AbstractTransport: public IProtocol {
public:

    AbstractTransport() = default;
    AbstractTransport(const AbstractTransport &other) = delete;
    AbstractTransport &operator=(const AbstractTransport &other) = delete;

    virtual void set_target(IProtocol *target) = 0;

};



}
