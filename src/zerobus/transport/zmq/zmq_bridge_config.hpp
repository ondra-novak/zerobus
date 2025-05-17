#pragma once

#include <functional>
#include <zmq.hpp>
#include "../../bridge.hpp"

namespace zerobus {


struct ZmqBridgeConfig {
    ///Specifies bridge mode (direction)
    BridgeOpMode mode = BridgeOpMode::bidirectional;
    ///Specifies count of threads created for this bridge
    unsigned int threads = 1;
    ///Specifies how much seconds inactivity causes removing of dead context
    /** For client this specifies value set for server, so it also
     * defines period of pings
     */
    unsigned int housekeeping_sec = 300;
    ///Allows to specify function for zmq socket decoration
    /** This can be used to set up encryption for example */
    std::function<void(zmq::socket_t &)> decorate_socket = {};
};
}
