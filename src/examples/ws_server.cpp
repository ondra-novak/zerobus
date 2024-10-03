
#include <zerobus/monitor.h>
#include <zerobus/functionref.h>
#include <zerobus/client.h>
#include <zerobus/bridge_tcp_client.h>
#include <zerobus/bridge_tcp_server.h>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace zerobus;

BridgeTCPServer::CustomPage load_page(std::string_view path) {
    if (!path.empty()) {
        path = path.substr(1);
        if (path.find('/') == path.npos) {
            if (path.empty()) path = "index.html";
            std::ifstream f((std::string(path)));
            if (!!f) {
                std::stringstream buffer;
                std::string_view ctx = "application/octet-stream";
                buffer << f.rdbuf();
                if (path.size() > 5 && path.substr(path.size()-5) == ".html") {
                    ctx = "text/html";
                }else if (path.size() > 3 && path.substr(path.size()-3) == ".js") {
                    ctx = "text/javascript";
                }
                return {
                    200, "OK", std::string(ctx), buffer.str()
                };
            } else {
                return {
                    404, "Not found", "text/plain","not found"
                };
            }
        }
    }
    return {
        403, "Forbidden", "text/plain",""
    };
}

int main() {
    unsigned int port = 12121;

    auto bus = Bus::create();
    BridgeTCPServer server(bus, "localhost:"+std::to_string(port));
    server.set_custom_page_callback(load_page);
    std::cout << "Opened at port:" << port << std::endl;
    std::cout << "Press enter to exit:";
    std::cin.get();



}
