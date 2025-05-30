#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace zerobus {

/// Message flags used for advanced routing, delivery control and prioritization.
enum class MsgFlags : std::uint8_t {
    /**
     * Normal priority — this is the default.
     *
     * Messages with normal priority may be discarded when the output buffer
     * reaches the high water mark (HWM).
     */
    priorityNormal = 0,

    /**
     * Low priority.
     *
     * Low priority messages are sent only if the output buffer is empty.
     * They may still be buffered in the internal TCP buffer. Once that buffer
     * is full, low priority messages are discarded (EWOULDBLOCK discards message)
     */
    priorityLow = 1,

    /**
     * High priority.
     *
     * High priority messages are guaranteed to be delivered unless there is a
     * connection issue. If the output buffer is full, sending such a message
     * will block and apply backpressure. If the peer does not accept the message
     * within the configured send timeout, the message is discarded and the
     * connection is closed.
     */
    priorityHigh = 2,

    /**
     * Request notification when a message is discarded.
     *
     * When this flag is set and a message is discarded due to buffer overflow,
     * a notification message is sent back to the original sender. This notification
     * has the same priority as the original message, so it may also be discarded.
     */
    discardNotify = 4,

    /**
     * Deliver the message to only single receiver.
     *
     * Used for multicast or group channels. Ensures that only single receiver
     * (selected non-deterministically) processes the message.
     *
     * Useful when multiple identical services are listening on the same channel
     * and only one of them should respond to a particular message.
     */
    singleReceiver = 8,

    /**
     * Kick the selected receiver out of the group.
     *
     * Extends the oneReceiver flag. Applicable only in groups with an owner.
     * The selected receiver is removed from the group after receiving the message,
     * preventing further messages from being delivered to them. The receiver
     * must explicitly request to be re-added by the group owner.
     *
     * This pattern is useful for "task-waiting" groups, where all members wait
     * for a task. Once a member receives a task, it is removed from the group
     * until it completes the task and requests to return.
     */
    kickOutReceiver = 16
};

/// Check if a flag is set in a flag set
template<MsgFlags F>
constexpr bool contains(MsgFlags value) {
    using T = std::underlying_type_t<MsgFlags>;
    return (static_cast<T>(value) & static_cast<T>(F)) != 0;
}

/// Combine two flags using bitwise OR
constexpr MsgFlags operator|(MsgFlags a, MsgFlags b) {
    using T = std::underlying_type_t<MsgFlags>;
    return static_cast<MsgFlags>(
        static_cast<T>(a) | static_cast<T>(b)
    );
}

constexpr std::string_view msg_flags_names[5] = {
        "priorityLow","priorityHigh","discardNotify","oneReceiver","kickOutReceiver"
};

template<typename Container>
requires(requires(Container c){
    {c.begin()};
    {c.end()};
    {c.insert(c.begin(), std::declval<std::string_view>().begin(), std::declval<std::string_view>().end())};
})
constexpr std::string_view to_string(MsgFlags flags, Container &buffer) {
    auto mask = std::underlying_type_t<MsgFlags>(1);
    auto val = std::underlying_type_t<MsgFlags>(flags);
    bool sep = false;
    constexpr std::string_view sepstr(" | ");
    constexpr std::string_view normal("priorityNormal");

    if (val == 0) {
        buffer.insert(buffer.end(), normal.begin(), normal.end());
    } else {
        for (std::string_view n: std::initializer_list<std::string_view>{
             "priorityLow","priorityHigh","discardNotify","oneReceiver","kickOutReceiver"
        }) {
            if (mask & val) {
                if (sep) buffer.insert(buffer.end(), sepstr.begin(), sepstr.end());
                else sep = true;
                buffer.insert(buffer.end(), n.begin(), n.end());
            }
        }
    }
    return {buffer.begin(), buffer.end()};

}



}
