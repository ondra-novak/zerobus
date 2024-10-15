#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif
#include "check.h"

#include <zerobus/client.h>
#include <zerobus/bridge_tcp_client.h>
#include <zerobus/bridge_tcp_server.h>
#include <zerobus/channel_notify.h>
#include <future>
#include <thread>

using namespace zerobus;


void direct_bridge_simple() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave = Bus::create();

    BridgeTCPServer server(master, "localhost:12121");
    BridgeTCPClient client(slave, "localhost:12121");

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

    bool w = channel_wait_for(slave, "reverse", std::chrono::seconds(2));
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

    BridgeTCPServer server(master,  "localhost:12121");
    BridgeTCPClient client1(slave1, "localhost:12121");
    BridgeTCPClient client2(slave2, "localhost:12121");

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
    bool w = channel_wait_for(slave1, "reverse", std::chrono::hours(2));
    CHECK(w);


    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");


}

void ws_key() {
    auto r = ws::calculate_ws_accept("dGhlIHNhbXBsZSBub25jZQ==");
    CHECK_EQUAL(r, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

}

class FlagRef {
public:
    std::atomic<bool> &flag;
    FlagRef(std::atomic<bool> &flag):flag(flag) {}
};

template<typename Base>
class BridgeCycleReport: public FlagRef, public Base {
public:
    template<typename ... Args>
    BridgeCycleReport(std::atomic<bool> &flag, Args ... args)
        :FlagRef(flag), Base(args...) {}
    virtual void cycle_detection(bool f) noexcept {
        if (f) {
            flag.store(true);
            flag.notify_all();
        }
    };
};

void detect_cycle_test() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();
    auto master2 = Bus::create();
    std::promise<std::string> result;
    std::atomic<bool> cycle ={false};

    BridgeCycleReport<BridgeTCPServer> server1(cycle, master,  "localhost:12121");
    BridgeCycleReport<BridgeTCPClient> client11(cycle, slave1, "localhost:12121");
    BridgeCycleReport<BridgeTCPClient> client12(cycle, slave2, "localhost:12121");
    BridgeCycleReport<BridgeTCPServer> server2(cycle, master2,  "localhost:12122");
    BridgeCycleReport<BridgeTCPClient> client21(cycle, slave1, "localhost:12122");



    auto sn = ClientCallback(slave1, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave2, [&](AbstractClient &, const Message &msg, bool){
            result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");
    bool w = channel_wait_for(slave1, "reverse", std::chrono::hours(2));
    CHECK(w);

    //close the cycle
    BridgeCycleReport<BridgeTCPClient> client22(cycle, slave2, "localhost:12122");

    cycle.wait(false);

    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");
}

class ReconnectClientTest: public FlagRef, public BridgeTCPClient {
public:
    template<typename F, typename ... Args>
    ReconnectClientTest(F &f, Args ... args):FlagRef(f), BridgeTCPClient(args...) {}
    virtual void lost_connection() {
        BridgeTCPClient::lost_connection();
        flag.store(true);
        flag.notify_all();
    }
};

void test_reconnect() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave = Bus::create();
    std::atomic<bool> flag = {false};

    std::promise<std::string> result;

    ReconnectClientTest client(flag, master, "localhost:12121");

    auto sn = ClientCallback(master, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave, [&](AbstractClient &, const Message &msg, bool){
        result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");

    flag.wait(false);

    BridgeTCPServer server(slave, "localhost:12121");

    bool w = channel_wait_for(slave, "reverse", std::chrono::hours(2));
    CHECK(w);


    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");

}

int main() {
#ifdef _WIN32
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    std::jthread timer([](std::stop_token tkn) {
        std::mutex mx;
        std::condition_variable cond;
        bool flag = false;
        std::stop_callback cb(tkn, [&]{
            std::lock_guard _(mx);
            flag = true;
            cond.notify_all();
        });
        std::unique_lock lk(mx);
        if (!cond.wait_for(lk, std::chrono::minutes(1), [&]{return flag;})) abort();
    });
    ws_key();
    direct_bridge_simple();
    two_hop_bridge();
    detect_cycle_test();
    test_reconnect();
}
