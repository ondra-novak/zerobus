#include "check.h"
#include <iostream>
#include <zerobus/bus.hpp>
#include <zerobus/transport/zmq/zmq_bridge_client.hpp>
#include <zerobus/transport/zmq/zmq_bridge_server.hpp>
#include <zerobus/channel_notify.hpp>

#include <future>
using namespace zerobus;

void direct_bridge_simple() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave = Bus::create();
    zmq::context_t ctx;

    ZmqBridgeServer server(master,ctx, "tcp://localhost:12121");
    ZmqBridgeClient client(slave,ctx, "tcp://localhost:12121");

    std::promise<std::string> result;

    auto sn = master.new_client([&](AbstractClient *c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c->send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave.new_client([&](AbstractClient *, const Message &msg, bool){
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

    zmq::context_t ctx;

    ZmqBridgeServer server(master,ctx, "tcp://localhost:12121",ZmqBridgeConfig{.threads=4});
    ZmqBridgeClient client1(slave1,ctx, "tcp://localhost:12121");
    ZmqBridgeClient client2(slave2,ctx, "tcp://localhost:12121");

    std::promise<std::string> result;

    auto sn = slave2.new_client([&](AbstractClient *c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c->send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave1.new_client([&](AbstractClient *, const Message &msg, bool){
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
