#include "check.h"

#include <zerobus/monitor.h>
#include <zerobus/client.h>
#include <zerobus/bridge_tcp_client.h>
#include <zerobus/bridge_tcp_server.h>
#include <future>

using namespace zerobus;


void direct_bridge_simple() {
    auto master = Bus::create();
    auto slave = Bus::create();

    BridgeTCPServer server(master,  "localhost:12121");
    BridgeTCPClient client(slave,  "localhost:12121");

    std::promise<std::string> result;

    auto sn = ClientCallback(master, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave, [&](AbstractClient &, const Message &msg, bool){
        result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");

    int cnt = 0;
    while (!slave.is_channel("reverse") && cnt < 1000) {
       std::this_thread::sleep_for(std::chrono::milliseconds(10));    //wait until route is propagated
       ++cnt;
    }
    CHECK_LESS(cnt,1000);   //must not take too long


    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");


}

void two_hop_bridge() {
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2= Bus::create();

    BridgeTCPServer server(master,  "localhost:12121");
    BridgeTCPClient client1(slave1,  "localhost:12121");
    BridgeTCPClient client2(slave2,  "localhost:12121");

    std::promise<std::string> result;

    auto sn = ClientCallback(slave2, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave1, [&](AbstractClient &, const Message &msg, bool){
        result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");

    int cnt = 0;
    while (!slave1.is_channel("reverse") && cnt < 1000) {
       std::this_thread::sleep_for(std::chrono::milliseconds(10));    //wait until route is propagated
       ++cnt;
    }
    CHECK_LESS(cnt,1000);   //must not take too long


    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");


}

void ws_key() {
    auto r = ws::calculate_ws_accept("dGhlIHNhbXBsZSBub25jZQ==");
    CHECK_EQUAL(r, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

}

int main() {
    ws_key();
    direct_bridge_simple();
    two_hop_bridge();
//    direct_bridge_cycle();

}
