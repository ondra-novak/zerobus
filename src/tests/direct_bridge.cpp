#include "check.h"

#include <zerobus/null_bridge.hpp>
#include <future>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <optional>
#include <zerobus/terminal.hpp>
using namespace zerobus;


void debug_output(std::string_view text) {
    std::cout << text << std::endl;
}


void direct_bridge_simple() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();

    DebugNullBridge br1(slave1, master, &debug_output, "SLAVE1", "MASTER");
    DebugNullBridge br2(slave2, master, &debug_output, "SLAVE2", "MASTER");
    std::string result;

    auto sn = slave1.new_terminal([&](auto &c, const Message &msg){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto sn2 = slave1.new_terminal([&](auto &c, const Message &msg){
        std::string s ( msg.get_content());
        s.push_back('x');
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave2.new_terminal([&](auto &c, const Message &msg){
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
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();
    std::string result;

    DebugNullBridge br1(slave1, master, &debug_output, "SLAVE1", "MASTER");
    DebugNullBridge br2(slave2, master, &debug_output, "SLAVE2", "MASTER");
    auto sn = slave1.new_terminal([&](auto &c, const Message &msg){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave2.new_terminal([&](auto &, const Message &msg){
        result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");

    DebugNullBridge br3(slave2, slave1, &debug_output, "SLAVE2", "SLAVE1");

    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
}

void detect_cycle_test2() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();
    auto master2 = Bus::create();
    std::promise<std::string> result;

    DebugNullBridge br1(slave1, master, &debug_output, "SLAVE1", "MASTER");
    DebugNullBridge br2(slave2, master, &debug_output, "SLAVE2", "MASTER");
    std::optional<DebugNullBridge<decltype(&debug_output)>> b3(std::in_place, master2, slave1, &debug_output, "MASTER2", "SLAVE1");



    auto sn =slave1.new_terminal([&](auto &c, const Message &msg){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave2.new_terminal([&](auto &, const Message &msg){
            result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");

    //close the cycle
    DebugNullBridge  b4(master2, slave2, &debug_output, "MASTER2", "SLAVE2");


    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");
    b3.reset();
}



void clear_path_test() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();
    std::string result;
    std::string rp;
    bool recvd_error = false;

    DebugNullBridge br1(slave1, master, &debug_output, "SLAVE1", "MASTER");
    DebugNullBridge br2(slave2, master, &debug_output, "SLAVE2", "MASTER");
    auto sn = slave1.new_terminal(overloaded{
        [&](auto &c, const ChannelMessage &msg){
            std::string s ( msg.get_content());
            rp = msg.get_sender();
            std::reverse(s.begin(), s.end());
            c.send_message(msg.get_sender(), s, msg.get_conversation());
        },[&](auto &, const Undelivered &) {
            recvd_error = true;
        }
    });
    auto cn= slave2.new_terminal([&](auto &, const Message &msg){
        result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");
    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
    cn.unsubscribe_all();
    bool r1 = sn.send_message(rp, "aaa"); //still should return true
    CHECK(recvd_error);
    bool r2 = sn.send_message(rp, "bbb"); //should return false
    CHECK(r1);
    CHECK(!r2);
}


void groups() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();


    DebugNullBridge br1(slave1, master, &debug_output, "SLAVE1", "MASTER");
    DebugNullBridge br2(slave2, master, &debug_output, "SLAVE2", "MASTER");

    std::string result;


    auto sn = slave2.new_terminal([&](Terminal &c, const ChannelMessage &msg){
            c.add_to_group("test_group", msg.get_sender(), msg.get_conversation());
            std::string s ( msg.get_content());
            std::reverse(s.begin(), s.end());
            c.send_message("test_group", s);
    });
    auto cn= slave1.new_terminal([&](Terminal &, const ChannelMessage &msg){
            result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");
    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
    sn.close_group("test_group");
    CHECK(!sn.send_message("test_group", "aaa"));
    CHECK(!cn.send_message("test_group", "aaa"));

}

void clear_path_group_test() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();
    std::string result;

    DebugNullBridge br1(slave1, master, &debug_output, "SLAVE1", "MASTER");
    DebugNullBridge br2(slave2, master, &debug_output, "SLAVE2", "MASTER");
    auto sn = slave2.new_terminal([&](Terminal &c, const ChannelMessage &msg){
            std::string s ( msg.get_content());
            std::reverse(s.begin(), s.end());
            c.add_to_group("gr", msg.get_sender(), msg.get_conversation());
            c.send_message("gr", s, msg.get_conversation());
    });
    auto cn= slave1.new_terminal([&](Terminal &, const ChannelMessage &msg){
            result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");
    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
    cn.unsubscribe_all();
    bool r1 = sn.send_message("gr", "aaa");
    CHECK(!r1);
}


int main() {
    direct_bridge_simple();
    direct_bridge_cycle();
    detect_cycle_test2();
    clear_path_test();
    groups();
    clear_path_group_test();

}



