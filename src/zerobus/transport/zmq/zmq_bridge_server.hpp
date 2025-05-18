#pragma once

#include "zmq_bridge_config.hpp"
#include "zmq_endpoint.hpp"
#include "zmq_mt_hlp.hpp"
#include "../../bus.hpp"
#include "../../bridge.hpp"
#include "../../binary_transport.hpp"
#include "../../utils/multithreads.hpp"


#include <thread>
#include <unordered_map>
#include <functional>
#include <shared_mutex>

namespace zerobus {

class ZmqBridgeServer {
public:

    ZmqBridgeServer(Bus bus,
            zmq::context_t &ctx,
            std::string address,
            ZmqBridgeConfig config = {});
    ~ZmqBridgeServer();

protected:

    class PeerContext;



    class PeerContext {
    public:

        PeerContext(ZmqBridgeServer &owner, std::string_view ident);

        char *output_start(std::size_t sz);
        void output_commit(std::size_t sz);

        void on_incoming_message(std::string_view msg);

        std::string_view get_ident() const {return _ident;}
        void set_sleeping(bool sleeping);
        std::chrono::system_clock::time_point get_last_activity() const;

    protected:


        ZmqBridgeServer &_owner;
        std::string _ident = {};
        BinaryTransport<OutputTypeProxy<PeerContext *> > *_parser = nullptr;
        std::optional<Bridge> _br;
        mutable std::mutex _mx;
        std::vector<char> _out_buffer;
        std::queue<std::string> _q;
        bool _sleeping = false;
        std::chrono::system_clock::time_point _last_activity;
    };


    Bus _bus;
    ZmqEndpoint _endpoint;
    BridgeOpMode _mode;
    unsigned int _housekeeping_sec;


    std::unordered_map<std::string_view, std::unique_ptr<PeerContext> > _peers;
    std::shared_mutex _mx;
    std::atomic<std::chrono::system_clock::time_point> _next_housekeeping;

    void do_housekeeping();
    void sleep_peer(std::string_view identity, bool sleep);
    void on_message(std::string_view data, std::string_view ident);
    std::chrono::system_clock::time_point on_timeout();
    void on_error(std::string_view ident);
    static std::unique_ptr<AbstractTransport> create_binary_transport(PeerContext *out,
            BinaryTransport<OutputTypeProxy<PeerContext *> >  *& in);

    friend class ZmqMtHelp<ZmqBridgeServer &>;
    ZmqMtHelp<ZmqBridgeServer &> _pool;

};

}
