#include "check.h"

#include <zerobus/channel_notify_listener.hpp>
#include <zerobus/bus.hpp>

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

    auto client1 = broker.new_client([&](auto,const Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r1 = true;
    });
    auto client2 = broker.new_client([&](auto,const Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r2 = true;
    });
    auto client3 = broker.new_client([&](auto,const Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r3 = true;
    });
    auto clientd = broker.new_client([&](AbstractClient *client, const Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        rd = true;
        client->get_bus().unsubscribe(client, channel_name);
    });
    broker.subscribe(&client1, channel_name);
    broker.subscribe(&clientd, channel_name);
    broker.subscribe(&client2, channel_name);
    broker.subscribe(&client3, channel_name);
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

    auto server = broker.new_client([&](AbstractClient *c, const Message &msg, bool ){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c->get_bus().send_message(c,msg.get_sender(), s);
    });
    auto client = broker.new_client([&](AbstractClient *, const Message &msg, bool ){
        result.append(std::string(msg.get_content()));
    });

    broker.subscribe(&server, "reverse");
    broker.send_message(&client,"reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
}

void testReqRep2() {

    auto broker = Bus::create();
    std::string result;

    auto server = broker.new_client([&](AbstractClient *c, const Message &msg, bool ){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c->get_bus().send_message(c, msg.get_sender(), s);
        c->get_bus().send_message(c, msg.get_sender(), s);
    });
    auto client = broker.new_client([&](AbstractClient *c, const Message &msg, bool ){
        result.append(std::string(msg.get_content()));
        c->get_bus().unsubscribe_all(c);

    });

    broker.subscribe(&server,"reverse");
    broker.send_message(&client, "reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
}

void testChannelForward() {
    auto broker = Bus::create();
    std::string result;

    auto node1 = broker.new_client([&](AbstractClient *c, const Message &msg, bool ){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c->get_bus().send_message(c,"c2", s);
    });
    auto node2 = broker.new_client([&](AbstractClient *, const Message &msg, bool ){
        result = std::string(msg.get_content());
    });

    broker.subscribe(&node1,"c1");
    broker.subscribe(&node2,"c2");
    broker.send_message(nullptr, "c1", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");

}

void testDialog() {
    auto broker = Bus::create();
    std::vector<std::string> test_data = {"abc","xyz","123","abba","xxx"};
    std::vector<std::string> test_expected = {"cba","zyx","321","abba","xxx"};
    std::vector<std::string> test_result;
    int pos = 0;


    auto server = broker.new_client([&](AbstractClient *c, const Message &msg, bool pm){
        if (pm) {
            std::string s ( msg.get_content());
            std::reverse(s.begin(), s.end());
            c->get_bus().send_message(c, msg.get_sender(), s);
        } else {
            c->get_bus().send_message(c, msg.get_sender(), "");
        }
    });
    auto client = broker.new_client([&](AbstractClient *c, const Message &msg, bool ){
        if (msg.get_channel() == "start_test") {
            pos = -1;
            c->get_bus().send_message(c,"reverse", "");
        } else {
            if (pos >= 0) {
                test_result.push_back(std::string(msg.get_content()));
            }
            ++pos;
            if (pos < static_cast<int>(test_data.size())) {
                c->get_bus().send_message(c, msg.get_sender(), test_data[pos]);
            }
        }
    });

    broker.subscribe(&server,"reverse");
    broker.subscribe(&client,"start_test");
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
