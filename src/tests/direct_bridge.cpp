#include "check.h"

#include <zerobus/monitor.h>
#include <zerobus/functionref.h>
#include <zerobus/client.h>
#include <zerobus/direct_bridge.h>
#include <future>

#include <algorithm>
#include <sstream>
#include <iomanip>
using namespace zerobus;


class VerboseBridge: public DirectBridge {
public:

    VerboseBridge(Bus b1, Bus b2): DirectBridge(std::move(b1),std::move(b2), false) {
        connect();
    }
    VerboseBridge(Bus b1, Bus b2, std::unique_ptr<Filter> flt): DirectBridge(std::move(b1),std::move(b2), false) {
        _b1.set_filter(std::move(flt));
        connect();
    }


protected:
    template<typename ... Args>
    void log(const Bridge &bs, Args && ... args) {
        auto &bt = select_other(bs);
        auto ptrs = bs.get_bus().get_handle().get();
        auto ids = (reinterpret_cast<std::uintptr_t>(ptrs) / 8) & 0xFFF;
        auto ptrt = bt.get_bus().get_handle().get();
        auto idt = (reinterpret_cast<std::uintptr_t>(ptrt) / 8) & 0xFFF;
        std::cout << std::setw(4) << ids << "->" << std::setw(4) << idt << ": ";
        (std::cout << ... << args);
        std::cout << std::endl;
    }

    virtual void on_send(const Bridge &source, Bridge::ChannelReset &&r) override {
        log(source, "RESET");
        DirectBridge::on_send(source, std::move(r));
    }
    virtual void on_send(const DirectBridge::Bridge &source, Message &&msg) override {
        log(source, "MESSAGE: sender: ", msg.get_sender(), " channel: ", msg.get_channel(),
                " content: ", msg.get_content(), " conversation: ", msg.get_conversation());
        DirectBridge::on_send(source, std::move(msg));
    }
    virtual void on_send(const DirectBridge::Bridge &source, Bridge::ChannelUpdate &&r) override {
        std::ostringstream chlist;
        char sep = ' ';
        for (auto c: r.lst) {
            chlist << sep << c;
            sep = ',';
        }
        log(source, "CHANNELS: ", r.op == AbstractBridge::Operation::add?"ADD":
                          r.op == AbstractBridge::Operation::erase?"ERASE":"REPLACE", chlist.view());
        DirectBridge::on_send(source, std::move(r));
    }
    virtual void on_send(const Bridge &source, Bridge::ClearPath &&r) override {
        log(source, "CLEAR_PATH: ",r.sender," -> ",r.receiver);
        DirectBridge::on_send(source, std::move(r));
    }
    virtual void cycle_detection(const DirectBridge::Bridge &source, bool state) noexcept override{
        if (state) log(source, "CYCLE DETECTED!");
        else log(source, "CYCLE cleared");
    }
    virtual void on_send(const Bridge &source, Bridge::CloseGroup &&g) override {
        log(source, "CLOSE_GROUP: ",g.group);
        DirectBridge::on_send(source, std::move(g));
    }
    virtual void on_send(const Bridge &source, Bridge::AddToGroup &&g) override {
        log(source, "ADD_TO_GROUP: ",g.target," -> ",g.group);
        DirectBridge::on_send(source, std::move(g));

    }
};

void direct_bridge_simple() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();

    VerboseBridge br1(slave1, master);
    VerboseBridge br2(slave2, master);
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
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave2, [&](AbstractClient &, const Message &msg, bool){
        result=std::string(msg.get_content());
    });

    sn.subscribe("reverse");

    VerboseBridge br3(slave2, slave1);


    cn.send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
}

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
    virtual bool outgoing(ChannelID id) const {
        return id == "reverse";
    }
};

void filter_channels() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();

    VerboseBridge br1(slave1, master, std::make_unique<TestFlt>());
    VerboseBridge br2(master, slave2, std::make_unique<TestFlt>());
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

    VerboseBridge br1(slave1, master, std::make_unique<TestFlt>());
    VerboseBridge br2(master, slave2, std::make_unique<TestFlt>());
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

int main() {
    direct_bridge_simple();
    direct_bridge_cycle();
    clear_path_test();
    filter_channels();
    groups();

}

