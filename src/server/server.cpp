
#include <iostream>
#include "embedded_js.h"


#include <zerobus/monitor.h>
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
        if (path.empty()) {
            return {200,"Ok","text/html", client_embedded_html};
        } else if (path == "client.js") {
            return {200,"Ok","text/javascript", client_embedded_js};
        } else {
            return {404, "Not found", "text/plain","not found"};
        }
    }    
    return {403, "Forbidden", "text/plain",""};
}


int main() {
    unsigned int port = 12121;

    auto bus = Bus::create();
    ClientCallback ping(bus, [](AbstractClient &c, const Message &msg, bool ){
        if (msg.get_sender().empty()) {
            std::cout << "Received anonymous message: " << msg.get_content() << std::endl;
        } else {
            std::cout << "Received message from: "<<msg.get_sender() << " - " << msg.get_content() << std::endl;
            c.send_message(msg.get_sender(), msg.get_content(), msg.get_conversation());
        }
    });
    ClientCallback timer(bus, [thr = std::jthread()](AbstractClient &c, const Message &msg, bool ) mutable {
        if (msg.get_sender().empty()) return;
        if (!thr.joinable()) {
            thr = std::jthread([&](std::stop_token tkn){
                while (!tkn.stop_requested()) {
                    auto tp = std::time(nullptr);
                    c.send_message("timer_data", std::to_string(tp),0);                    
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            });
        };
        c.add_to_group("timer_data", msg.get_sender());
    });
    ping.subscribe("ping");
    timer.subscribe("timer");
    BridgeTCPServer server(bus, "localhost:"+std::to_string(port));
    server.set_custom_page_callback(load_page);
    std::cout << "Opened at port:" << port << std::endl;
    std::cout << "Press enter to exit:";
    std::cin.get();



}
