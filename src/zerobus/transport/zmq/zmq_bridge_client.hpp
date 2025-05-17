#pragma once

#include "zmq_bridge_config.hpp"
#include "../../binary_transport.hpp"
#include "../../utils/multithreads.hpp"
#include "zmq_endpoint.hpp"

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

    utils::MultiThread _mthr;
    mutable std::mutex _mx;
    std::vector<char> _out_buffer;

    unsigned int _ping_interval;

    void worker(const bool &kf);

    char *output_start(std::size_t sz);
    void output_commit(std::size_t sz);

    friend struct OutputTypeProxy<ZmqBridgeClient *>;


    std::unique_ptr<AbstractTransport> create_binary_transport(
            BinaryTransport<OutputTypeProxy<ZmqBridgeClient *> >  *& in);

    void message_received(std::string_view msg);


    void send_ping();
};


}
