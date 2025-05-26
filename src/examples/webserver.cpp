#include <zerobus/transport/ws_epoll/ws_bridge.hpp>

int main(int argc, char **argv) {

    if (argc < 3) {
        std::puts("Needs :<port> <document root path>");
        return 1;
    }

    std::string addrport(argv[1]);
    auto bus = zerobus::Bus::create();
    zerobus::WsBridge bridge(bus, {
            .mode = zerobus::BridgeOpMode::isolated,
            .document_root = std::filesystem::path(argv[2])
    });
    bridge.bind(addrport);
    std::printf("WebServer running at: %s\n", addrport.c_str());
    std::puts("Press ENTER to exit");
    std::getchar();
    return 0;
}
