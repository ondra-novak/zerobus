#pragma once


#include "../../bridge.hpp"

#include <filesystem>
#include <functional>
#include <optional>

namespace zerobus {



struct WsBridgeConfig {
    ///Specifies bridge mode (direction)
    BridgeOpMode mode = BridgeOpMode::bidirectional;
    ///Specifies count of threads created for this bridge
    unsigned int threads = 1;
    ///allows to specify filter for messages
    MsgFilterFactory filter = {};
    ///path must match GET request
    std::string_view endpoint_path = "/";
    ///specifies www document root, for non ws requests (optional)
    std::optional<std::filesystem::path> document_root;
};
}
