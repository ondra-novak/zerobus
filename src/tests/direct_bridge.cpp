#include "check.h"

#include <zerobus/monitor.h>
#include <zerobus/client.h>
#include <zerobus/direct_bridge.h>
#include <future>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <optional>
using namespace zerobus;


class VerboseBridge: public DirectBridge {
public:

    VerboseBridge(Bus b1, Bus b2): DirectBridge(std::move(b1),std::move(b2), false) {
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
        std::cout << "+-";
        for (int i = 1; i < level; ++i) std::cout << '-';
        (std::cout << ... << args);
        std::cout << std::endl;
    }

    static int level;
public:
    void lock() {
        level++;
    }
    void unlock() {
        level--;
    }
protected:
    virtual void on_send(const Bridge &source, const Bridge::ChannelReset &r) override {
        std::lock_guard _(*this);
        log(source, "RESET");
        DirectBridge::on_send(source, std::move(r));
    }
    virtual void on_send(const DirectBridge::Bridge &source, const Message &msg) override {
        std::lock_guard _(*this);
        log(source, "MESSAGE: sender: ", msg.get_sender(), " channel: ", msg.get_channel(),
                " content: ", msg.get_content(), " conversation: ", msg.get_conversation());
        DirectBridge::on_send(source, msg);
    }
    virtual void on_send(const DirectBridge::Bridge &source, const Bridge::ChannelUpdate &r) override {
        std::lock_guard _(*this);
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
    virtual void on_send(const Bridge &source, const Bridge::ClearPath &r) override {
        std::lock_guard _(*this);
        log(source, "CLEAR_PATH: ",r.sender," -> ",r.receiver);
        DirectBridge::on_send(source, std::move(r));
    }
    virtual void cycle_detection(const DirectBridge::Bridge &source, bool state) noexcept override{
        std::lock_guard _(*this);
        if (state) log(source, "CYCLE DETECTED!");
        else log(source, "CYCLE cleared");
    }
    virtual void on_send(const Bridge &source, const Bridge::CloseGroup &g) override {
        std::lock_guard _(*this);
        log(source, "CLOSE_GROUP: ",g.group);
        DirectBridge::on_send(source, std::move(g));
    }
    virtual void on_send(const Bridge &source, const Bridge::AddToGroup &g) override {
        std::lock_guard _(*this);
        log(source, "ADD_TO_GROUP: ",g.target," -> ",g.group);
        DirectBridge::on_send(source, std::move(g));
    }
    virtual void on_send(const Bridge &source, const Bridge::GroupEmpty &g) override {
        std::lock_guard _(*this);
        log(source, "GROUP_EMPTY: ",g.group);
        DirectBridge::on_send(source, std::move(g));
    }
    virtual void on_send(const Bridge &source, const Bridge::UpdateSerial &g) override {
        std::lock_guard _(*this);
        log(source, "UPDATE_SERIAL: ",g.serial);
        DirectBridge::on_send(source, std::move(g));
    }
};


int VerboseBridge::level = 0;

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

void detect_cycle_test2() {
    std::cout << __FUNCTION__ << std::endl;
    auto master = Bus::create();
    auto slave1 = Bus::create();
    auto slave2 = Bus::create();
    auto master2 = Bus::create();
    std::promise<std::string> result;

    VerboseBridge b1(master, slave1);
    VerboseBridge b2(master, slave2);
    std::optional<VerboseBridge> b3(std::in_place, master2, slave1);



    auto sn = ClientCallback(slave1, [&](AbstractClient &c, const Message &msg, bool){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= ClientCallback(slave2, [&](AbstractClient &, const Message &msg, bool){
            result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");

    //close the cycle
    VerboseBridge b4(master2, slave2);


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

int main() {
    direct_bridge_simple();
    direct_bridge_cycle();
    detect_cycle_test2();
    clear_path_test();
    filter_channels();
    groups();
    clear_path_group_test();
}



