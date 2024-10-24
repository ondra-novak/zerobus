
#include <iostream>
#include "embedded_js.h"
#include <zerobus/monitor.h>
#include <zerobus/client.h>
#include <zerobus/bridge_tcp_client.h>
#include <zerobus/bridge_tcp_server.h>
#include <zerobus/http_utils.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

using namespace zerobus;

coroutine load_page(ConnHandle conn, std::shared_ptr<INetContext> context, std::string_view hdr_data, std::string_view) {
    auto stream = Stream::create(conn, std::move(context));
    auto first_line = parse_http_header(hdr_data, [](const auto &...){});
    HttpRequestLine req = parse_http_request_line(first_line);
    int code = 404;
    std::string_view message = "Not found";
    std::string_view ctx = "text/html;charset=utf-8";
    std::string_view body = "<h1>404 Not found</h1>";

    if (icmp(req.method,"GET")) {
        if (!req.path.empty()) {
            auto path = req.path.substr(1);
            if (path.empty()) {
                code = 200;
                message = "Ok";
                body = client_embedded_html;
            } else if (path == "client.js") {
                code = 200;
                message = "Ok";
                body = client_embedded_js;
                ctx = "text/javascript";
            }
        }
    } else {
        code = 405;
        message = "Method not allowed\r\nAllow: GET";
        body = "<h1>405 Method not allowed<h1>";
    }
    std::ostringstream out;
    out << req.version << " " << code << " " << message << "\r\n"
            "Content-Type: " << ctx << "\r\n"
            "Server: zerobus\r\n"
            "Content-Length: " << body.size()  <<"\r\n"
            "Connection: close\r\n"
            "\r\n" << body;

    co_await stream->write(out.view());
}


/*
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

*/
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
    server.set_http_server_fn([](auto ... args){
        load_page(args...);
    });
  //  server.set_custom_page_callback(load_page);
    std::cout << "Opened at port:" << port << std::endl;
    std::cout << "Press enter to exit:";
    std::cin.get();



}
