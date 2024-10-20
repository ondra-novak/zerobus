#include <atomic>

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
#include <stop_token>

using namespace zerobus;


void bridge_simple(std::string process_path) {
    std::cout << __FUNCTION__ << std::endl;
    auto slave = Bus::create();

    std::string cmdline = "\"" + process_path + "\" child";

    auto ctx = make_network_context();
    std::stop_source stpreq;
    std::atomic<bool> exit_wait = {false};
    auto pp = spawn_process(ctx, cmdline, stpreq.get_token(), [&](int st){
        std::cout << "Child exited with code: " << st << std::endl;
        exit_wait = true;
        exit_wait.notify_all();
    });

    BridgePipe b2(slave, ctx, pp.read, pp.write);

    std::promise<std::string> result;

    auto cn= ClientCallback(slave, [&](AbstractClient &, const Message &msg, bool){
        result.set_value(std::string(msg.get_content()));
    });

    bool w = channel_wait_for(slave, "reverse", std::chrono::seconds(2));
    CHECK(w);


    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");

    stpreq.request_stop();

    exit_wait.wait(false);

}

void start_child () {
    auto master = Bus::create();
    auto ctx = make_network_context(1);
    auto h_read = ctx->connect(SpecialConnection::stdin);
    auto h_write = ctx->connect(SpecialConnection::stdout);
    BridgePipe b1(master, ctx, h_read, h_write);

    auto sn = ClientCallback(master, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });

    sn.subscribe("reverse");
    std::this_thread::sleep_for(std::chrono::hours(2));
}

int main(int argc, char **argv) {
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

    if (argc == 2 && std::string_view(argv[1]) == "child") {
        start_child();
    } else {
        bridge_simple(argv[0]);
    }
}
