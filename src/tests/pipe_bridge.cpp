#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif
#include "check.h"

#include <zerobus/client.h>
#include <zerobus/bridge_pipe.h>
#include <zerobus/channel_notify.h>
#include <future>
#include <thread>

using namespace zerobus;


void direct_bridge_simple() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave = Bus::create();

    auto ctx = make_network_context();
    auto p1 = ctx->create_pipe();
    auto p2 = ctx->create_pipe();

    BridgePipe b1(master, ctx, p1.read, p2.write);
    BridgePipe b2(slave, ctx, p2.read, p1.write);

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
    direct_bridge_simple();
}
