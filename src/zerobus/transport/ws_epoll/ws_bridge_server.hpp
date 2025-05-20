#pragma once

#include "ws_bridge_config.hpp"
#include "ws_endpoint.hpp"
#include "../../bus.hpp"
#include "../../bridge.hpp"
#include "../../binary_transport.hpp"
#include "../../utils/multithreads.hpp"


#include <thread>
#include <unordered_map>
#include <functional>
#include <queue>
#include <shared_mutex>

namespace zerobus {

class WsBridgeServer {
public:

    WsBridgeServer(Bus bus,
            std::string address,
            WsBridgeConfig config = {});
    ~WsBridgeServer();

protected:

    class PeerContext;



    class PeerContext {
    public:

        PeerContext(WsBridgeServer &owner, std::string_view ident);

        char *output_start(std::size_t sz);
        void output_commit(std::size_t sz);

        void on_incoming_message(std::string_view msg, WsEndpoint::Handle h);

        std::string_view get_ident() const {return _ident;}
        void set_sleeping(bool sleeping);
        std::chrono::system_clock::time_point get_last_activity() const;

    protected:


        WsBridgeServer &_owner;
        WsEndpoint::Handle _h = {};
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
    WsEndpoint _endpoint;
    BridgeOpMode _mode;
    unsigned int _housekeeping_sec;


    std::unordered_map<std::string_view, std::unique_ptr<PeerContext> > _peers;
    std::shared_mutex _mx;
    std::atomic<std::chrono::system_clock::time_point> _next_housekeeping;

    void do_housekeeping();
    void sleep_peer(std::string_view identity, bool sleep);
    void on_message(std::string_view data, std::string_view ident, WsEndpoint::Handle h);
    std::chrono::system_clock::time_point on_timeout();
    void on_error(std::string_view ident);
    static std::unique_ptr<AbstractTransport> create_binary_transport(PeerContext *out,
            BinaryTransport<OutputTypeProxy<PeerContext *> >  *& in);


    utils::MultiThread _pool;

    void worker(std::stop_token stp, const bool &kf);

};

}
