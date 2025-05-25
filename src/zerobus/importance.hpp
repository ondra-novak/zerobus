#pragma once


namespace zerobus {

/**
 * @enum Importance
 * @brief Represents the importance level of a message, which determines how it is handled under various conditions.
 *
 * - `low`: Messages with low importance. These messages can be discarded when the TCP buffer is full.
 *          They are not put into the output buffer, so if the TCP buffer is full, they are discarded.
 * - `normal`: Messages with medium importance. These messages can be discarded when the TCP buffer is full
 *             and the High Water Mark (HWM) of the external buffer is reached.
 * - `high`: Messages with high importance. These messages apply backpressure, causing blocking sends.
 *           They are not discarded unless the send timeout is reached.
 * - `notify`: Applies a flag that causes a notification to be sent to the sender when the message is discarded.
 * - `_ntf` variants (e.g., `low_ntf`, `normal_ntf`, `high_ntf`): These are combinations of the original importance
 *   levels (`low`, `normal`, `high`) with the `notify` flag, meaning a notification will be sent to the sender
 *   if the message is discarded.
 */
enum class Importance: std::uint8_t {
    ///Importance is not set
    /** Depends on bridge how to treat this message */
    not_set = 0,
    ///Messages with low importance
    /** These messages can be discarded when the TCP buffer is full.
     * They are not put into the output buffer, so if the TCP buffer is full, they are discarded.
     */
    low = 1,
    ///Messages with normal importance
    /** These messages can be discarded when the TCP buffer is full
    *  and the High Water Mark (HWM) of the external buffer is reached.
    */
    normal = 2,
    ///Messages with high importance
    /**  These messages apply backpressure, causing blocking sends.
         They are not discarded unless the send timeout is reached.
    */
    high = 3,
    ///Applies a flag that causes a notification to be sent to the sender when the message is discarded.
    notify = 4,
    ///low+notify
    low_ntf = 5,
    ///normal+notify
    normal_ntf = 6,
    ///high+notify
    high_ntf = 7
};


constexpr Importance operator|(Importance a, Importance b) {
    return static_cast<Importance>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr Importance operator&(Importance a, Importance b) {
    return static_cast<Importance>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

constexpr Importance importance_mask  = Importance::low | Importance::normal | Importance::high;

}
