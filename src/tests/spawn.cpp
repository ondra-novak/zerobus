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

    std::atomic<bool> exit_wait = {false};
    auto ctx = make_network_context();
    std::stop_source stpreq;

    BridgePipe b2 = BridgePipe::connect_process(slave, cmdline, stpreq.get_token(), [&](int st){
        std::cout << "Child exited with code: " << st << std::endl;
        exit_wait = true;
        exit_wait.notify_all();
    });

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
    BridgePipe b1 = BridgePipe::connect_stdinout(master);

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
