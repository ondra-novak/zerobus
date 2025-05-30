#include "check.h"
#include <iostream>
#include <zerobus/transport/ws_epoll/ws_bridge.hpp>
#include <zerobus/channel_notify.hpp>

#include <future>
#include <zerobus/terminal.hpp>
using namespace zerobus;

static std::string address("localhost:12121");

void direct_bridge_simple() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave = Bus::create();
    WsBridge server(master, {});
    WsBridge client(slave, {});
    server.bind(address);
    client.connect(address);


    std::promise<std::string> result;

    auto sn = master.new_terminal([&](auto &c, const Message &msg){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave.new_terminal([&](auto &, const Message &msg){
        result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");

    bool w = channel_wait_for(slave, "reverse", std::chrono::hours(2));
    CHECK(w);


    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");
}
void two_hop_bridge() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2= Bus::create();

    WsBridge server(master, {});
    WsBridge client1(slave1, {});
    WsBridge client2(slave2, {});
    server.bind(address);
    client1.connect(address);
    client2.connect(address);

    std::promise<std::string> result;

    auto sn = slave2.new_terminal([&](auto &c, const Message &msg){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave1.new_terminal([&](auto &, const Message &msg){
        result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");
    bool w = channel_wait_for(slave1, "reverse", std::chrono::hours(2));
    CHECK(w);


    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");


}

int main() {
    direct_bridge_simple();
    two_hop_bridge();
}
