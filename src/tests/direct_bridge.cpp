#include "check.h"

#include <zerobus/bus.hpp>
#include <zerobus/null_bridge.hpp>
#include <future>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <optional>
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

    auto sn = slave1.new_client([&](AbstractClient *c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c->send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto sn2 = slave1.new_client([&](AbstractClient *c, const Message &msg, bool){
        std::string s ( msg.get_content());
        s.push_back('x');
        c->send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave2.new_client([&](AbstractClient *c, const Message &msg, bool){
        if (msg.get_conversation() == 0) {
            c->send_message("addx", msg.get_content(), 1);
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
    auto sn = slave1.new_client([&](AbstractClient *c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c->send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave2.new_client([&](AbstractClient *, const Message &msg, bool){
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



    auto sn =slave1.new_client([&](AbstractClient *c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c->send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= slave2.new_client([&](AbstractClient *, const Message &msg, bool){
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

#if 0

void clear_path_test() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();
    std::string result;
    std::string rp;

    VerboseBridge br1(slave1, master);
    VerboseBridge br2(slave2, master);
    auto sn = ClientCallback(slave1, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        rp = msg.get_sender();
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave2, [&](AbstractClient &, const Message &msg, bool){
        result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");
    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
    cn.unsubscribe_all();
    bool r1 = sn.send_message(rp, "aaa"); //still should return true (as we know detecting not delivering)
    bool r2 = sn.send_message(rp, "bbb"); //should return false
    CHECK(r1);
    CHECK(!r2);
}

class TestFlt: public Filter {
public:
    virtual bool on_outgoing(ChannelID id) override{
        return id == "reverse";
    }
};

void filter_channels() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();

    std::unique_ptr<Filter> flt1 = std::make_unique<TestFlt>();
    std::unique_ptr<Filter> flt2 = std::make_unique<TestFlt>();
    VerboseBridge br1(slave1, master);
    VerboseBridge br2(master, slave2);
    br1.getBridge1().set_filter(flt1);
    br2.getBridge1().set_filter(flt2);
    std::string result;

    auto sn = ClientCallback(slave2, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave1, [&](AbstractClient &, const Message &msg, bool){
        result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");
    sn.subscribe("not_pass");

    CHECK(cn.is_channel("reverse"));
    CHECK(!cn.is_channel("notpass"));

    auto r = cn.send_message("not_pass", "ahoj svete");
    CHECK(!r);
    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");

}

void groups() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();

    std::unique_ptr<Filter> flt1 = std::make_unique<TestFlt>();
    std::unique_ptr<Filter> flt2 = std::make_unique<TestFlt>();

    VerboseBridge br1(slave1, master);
    VerboseBridge br2(master, slave2);
    br1.getBridge1().set_filter(flt1);
    br2.getBridge1().set_filter(flt2);

    std::string result;


    auto sn = ClientCallback(slave2, [&](AbstractClient &c, const Message &msg, bool){
        c.add_to_group("test_group", msg.get_sender());
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message("test_group", s);
    });
    auto cn= ClientCallback(slave1, [&](AbstractClient &, const Message &msg, bool){
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

    VerboseBridge br1(slave1, master);
    VerboseBridge br2(slave2, master);
    auto sn = ClientCallback(slave1, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.add_to_group("gr", msg.get_sender());
        c.send_message("gr", s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave2, [&](AbstractClient &, const Message &msg, bool){
        result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");
    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
    cn.unsubscribe_all();
    bool r1 = sn.send_message("gr", "aaa");
    CHECK(!r1);
}

class AuthFilter: public Filter {
public:

    virtual bool on_incoming(ChannelID id) {
        return authorized || id == "auth";
    }
    virtual bool on_outgoing(ChannelID) {
        return authorized;  //don't send messages to unauthorized peers
    }
    virtual bool on_outgoing_add_to_group(ChannelID group_name, ChannelID) {
        if (group_name == "authorized_peers") {
            authorized = true;
            set_rule_changed();
        }
        return authorized;
    }
    virtual bool on_outgoing_close_group(ChannelID group_name) {
        if (group_name == "authorized_peers") {
            authorized = false;
            set_rule_changed();
        }
        return true;
    }

protected:
    bool authorized = false;
};

void authorize() {
    auto master = Bus::create();
    auto slave1 = Bus::create();
    VerboseBridge br1(slave1, master);
    br1.getBridge2().set_filter(std::make_unique<AuthFilter>());
    std::string result;

    auto sn = ClientCallback(master, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto an = ClientCallback(master, [&](AbstractClient &c, const Message &msg, bool){
        //example of auth;
        if (msg.get_content() == "let me in") {
            c.add_to_group("authorized_peers", msg.get_sender());
        }
    });
    auto gn = ClientCallback(master, [&](AbstractClient &c, const Message &msg, bool){
        //example of auth;
        if (msg.get_content() == "") {
            c.add_to_group("gr", msg.get_sender());
        }
    });
    auto cn= ClientCallback(slave1, [&](AbstractClient &, const Message &msg, bool){
        result=std::string(msg.get_content());
    });

    master.subscribe(&sn, "reverse");
    master.subscribe(&an, "auth");
    master.subscribe(&gn, "sub");

    CHECK(!slave1.is_channel("reverse"));
    CHECK(slave1.is_channel("auth"));
    cn.send_message("auth","let me in");
    CHECK(slave1.is_channel("reverse"));
    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");

    master.close_group(&an, "authorized_peers");
    CHECK(!slave1.is_channel("reverse"));
    CHECK(slave1.is_channel("auth"));


    cn.send_message("auth","let me in");
    cn.send_message("sub","");
    bool b = gn.send_message("gr","1");
    CHECK(b);
    CHECK_EQUAL(result, "1");
    b = gn.send_message("gr","2");
    CHECK(b);
    CHECK_EQUAL(result, "2");
    slave1.unsubscribe(&cn, "authorized_peers");
    b = gn.send_message("gr","3");
    CHECK(!b);




}

#endif

int main() {
    direct_bridge_simple();
    direct_bridge_cycle();
    detect_cycle_test2();
#if 0
    clear_path_test();
    filter_channels();
    groups();
    clear_path_group_test();
    authorize();
#endif

}



