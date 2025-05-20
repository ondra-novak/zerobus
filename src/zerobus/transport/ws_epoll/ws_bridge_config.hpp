#pragma once

#include "../../bridge.hpp"

namespace zerobus {


struct WsBridgeConfig {
    ///Specifies bridge mode (direction)
    BridgeOpMode mode = BridgeOpMode::bidirectional;
    ///Specifies count of threads created for this bridge
    unsigned int threads = 1;
    ///Specifies how much seconds inactivity causes removing of dead context
    /** For client this specifies value set for server, so it also
     * defines period of pings
     */
    unsigned int housekeeping_sec = 300;
};
}
