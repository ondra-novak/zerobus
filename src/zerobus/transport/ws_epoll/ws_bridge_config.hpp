#pragma once


#include "../../bridge.hpp"

#include <filesystem>
#include <functional>
#include <optional>

namespace zerobus {


struct TLSCertificate {
};


struct WsBridgeConfig {
    ///Specifies bridge mode (direction)
    BridgeOpMode mode = BridgeOpMode::bidirectional;
    ///Specifies count of threads created for this bridge
    unsigned int threads = 1;
    ///Specifies interval of checking inactivity
    /**
     * When peer is active, it resets inactivity flag. This
     * value specifies interval of setting inactivity flag. If
     * If the inactivity flag is set, ping is send. If there
     * is no activity after ping after next interval the peer is disconnected
     *
     * 1) inactivity flag is reset - sets it
     * 2) inactivity flag is set - sends ping - the pong should reset fla
     * 3) inactivity flag is set after ping - peer is disconnected
     */
    unsigned int keep_alive_interval_sec = 100;
    ///Specifies high watermark level in bytes
    /**
     * For normal importance messages, when high watermark level is reached, further
     * messages are discarded. For high importance messages, backpressure is
     * applied, until send_timeout is reached
     */
    unsigned int hwm_bytes = 65536;
    ///Specifies send timeout when backpressure is applied.
    /**
     * During this time, at least one byte must be departed otherwise
     * peer is disconnected. This is applied for high importance messages when
     * output buffer is full (low and normal important messages are discarded)
     */
    unsigned int send_timeout_ms = 1500;
    ///allows to specify filter for messages
    MsgFilterFactory filter = {};
    ///path must match GET request
    std::string_view endpoint_path = "/";
    ///specifies www document root, for non ws requests (optional)
    std::optional<std::filesystem::path> document_root;
    ///set this option true to inicialize TLS
    bool use_tls = false;
    ///set certificate for server node / client node to verify if set otherwise system store
    std::optional<std::filesystem::path> certificate_pem = {};
    ///set private key for server node (mandatory for server node)
    std::optional<std::filesystem::path> private_key_pem = {};
};
}
