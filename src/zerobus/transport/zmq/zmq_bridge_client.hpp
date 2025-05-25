#pragma once

#include "zmq_bridge_config.hpp"
#include "../../binary_transport.hpp"
#include "../../utils/multithreads.hpp"
#include "zmq_endpoint.hpp"
#include "zmq_mt_hlp.hpp"

namespace zerobus {




class ZmqBridgeClient {
public:

    ZmqBridgeClient(Bus bus,
            zmq::context_t &ctx,
            std::string address,
            ZmqBridgeConfig config = {});
    ~ZmqBridgeClient();


protected:

    ZmqEndpoint _endpoint;
    std::optional<Bridge> _br;
    BinaryTransport<OutputTypeProxy<ZmqBridgeClient *> > *_parser = nullptr;

    mutable std::mutex _mx;
    std::vector<char> _out_buffer;
    unsigned int _ping_interval;

    void on_message(std::string_view data, std::string_view ident);
    std::chrono::system_clock::time_point on_timeout();
    void on_error(std::string_view ident);

    friend class ZmqMtHelp<ZmqBridgeClient &>;

    ZmqMtHelp<ZmqBridgeClient &> _pool;


    char *output_start(std::size_t sz, Importance imp);
    DeliveryError output_commit(std::size_t sz, Importance imp);

    friend struct OutputTypeProxy<ZmqBridgeClient *>;


    std::unique_ptr<AbstractTransport> create_binary_transport(
            BinaryTransport<OutputTypeProxy<ZmqBridgeClient *> >  *& in);



    void send_ping();
};


}
