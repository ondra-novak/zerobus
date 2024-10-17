#include "check.h"

#include <zerobus/monitor.h>
#include <zerobus/bridge.h>
#include <zerobus/client.h>

#include <algorithm>
using namespace zerobus;

void testLocalBus() {
    auto broker = Bus::create();
    bool r1 = false;
    bool r2 = false;
    bool r3 = false;
    bool rd = false;
    constexpr std::string_view channel_name = "test";
    constexpr std::string_view message = "msg";

    auto client1 = ClientCallback(broker,[&](auto &,const Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r1 = true;
    });
    auto client2 = ClientCallback(broker,[&](auto &,const Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r2 = true;
    });
    auto client3 = ClientCallback(broker,[&](auto &,const Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r3 = true;
    });
    auto clientd = ClientCallback(broker,[&](auto &client, const Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        rd = true;
        client.unsubscribe(channel_name);
    });
    client1.subscribe(channel_name);
    clientd.subscribe(channel_name);
    client2.subscribe(channel_name);
    client3.subscribe(channel_name);
    broker.send_message(nullptr, channel_name, message);
    CHECK(r1);
    CHECK(r2);
    CHECK(r3);
    CHECK(rd);
    r1 = false;
    r2 = false;
    r3 = false;
    rd = false;
    broker.send_message(nullptr, channel_name, message);
    CHECK(r1);
    CHECK(r2);
    CHECK(r3);
    CHECK(!rd);
}

void testReqRep() {

    auto broker = Bus::create();
    std::string result;

    auto server = ClientCallback(broker, [&](AbstractClient &c, const Message &msg, bool ){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s);
    });
    auto client = ClientCallback(broker, [&](AbstractClient &, const Message &msg, bool ){
        result.append(std::string(msg.get_content()));
    });

    server.subscribe("reverse");
    client.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
}

void testReqRep2() {

    auto broker = Bus::create();
    std::string result;

    auto server = ClientCallback(broker, [&](AbstractClient &c, const Message &msg, bool ){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s);
        c.send_message(msg.get_sender(), s);
    });
    auto client = ClientCallback(broker, [&](AbstractClient &c, const Message &msg, bool ){
        result.append(std::string(msg.get_content()));
        c.unsubscribe_all();

    });

    server.subscribe("reverse");
    client.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
}

void testChannelForward() {
    auto broker = Bus::create();
    std::string result;

    auto node1 = ClientCallback(broker, [&](AbstractClient &c, const Message &msg, bool ){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message("c2", s);
    });
    auto node2 = ClientCallback(broker, [&](AbstractClient &, const Message &msg, bool ){
        result = std::string(msg.get_content());
    });

    node1.subscribe("c1");
    node2.subscribe("c2");
    broker.send_message(nullptr, "c1", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");

}

void testDialog() {
    auto broker = Bus::create();
    std::vector<std::string> test_data = {"abc","xyz","123","abba","xxx"};
    std::vector<std::string> test_expected = {"cba","zyx","321","abba","xxx"};
    std::vector<std::string> test_result;
    int pos = 0;


    auto server = ClientCallback(broker, [&](AbstractClient &c, const Message &msg, bool pm){
        if (pm) {
            std::string s ( msg.get_content());
            std::reverse(s.begin(), s.end());
            c.send_message(msg.get_sender(), s);
        } else {
            c.send_message(msg.get_sender(), "");
        }
    });
    auto client = ClientCallback(broker, [&](AbstractClient &c, const Message &msg, bool ){
        if (msg.get_channel() == "start_test") {
            pos = -1;
            c.send_message("reverse", "");
        } else {
            if (pos >= 0) {
                test_result.push_back(std::string(msg.get_content()));
            }
            ++pos;
            if (pos < static_cast<int>(test_data.size())) {
                c.send_message(msg.get_sender(), test_data[pos]);
            }
        }
    });

    server.subscribe("reverse");
    client.subscribe("start_test");
    broker.send_message(nullptr,"start_test","");
    CHECK(test_expected == test_result);


}

int main() {
    testLocalBus();
    testReqRep();
    testReqRep2();
    testChannelForward();
    testDialog();


}
