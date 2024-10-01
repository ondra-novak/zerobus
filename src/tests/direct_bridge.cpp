#include "check.h"

#include <zerobus/monitor.h>
#include <zerobus/functionref.h>
#include <zerobus/client.h>
#include <zerobus/direct_bridge.h>
#include <future>

#include <algorithm>
using namespace zerobus;


void direct_bridge_simple() {
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();

    DirectBridge br1(slave1, master);
    DirectBridge br2(slave2, master);
    std::string result;

    auto sn = ClientCallback(slave1, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto sn2 = ClientCallback(slave1, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        s.push_back('x');
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave2, [&](AbstractClient &c, const Message &msg, bool){
        if (msg.get_conversation() == 0) {
            c.send_message("addx", msg.get_content(), 1);
        } else {
            result=std::string(msg.get_content());
        }
    });

    sn.subscribe("reverse");
    sn2.subscribe("addx");


    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs johax");


}

void direct_bridge_cycle() {
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();
    std::string result;

    DirectBridge br1(slave1, master);
    DirectBridge br2(slave2, master);
    DirectBridge br3(slave2, slave1);
    auto sn = ClientCallback(slave1, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave2, [&](AbstractClient &, const Message &msg, bool){
        result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");

    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");



}

int main() {
    direct_bridge_simple();
    direct_bridge_cycle();

}
