#include "zmq_bridge_client.hpp"

namespace zerobus {

static zmq::socket_t prepare_socket(
        zmq::context_t &ctx,
        const std::string &address,
        const std::function<void(zmq::socket_t &)> &decorate) {

    zmq::socket_t out(ctx, zmq::socket_type::dealer);
    if (decorate) decorate(out);
    out.connect(address);
    return out;

}

std::unique_ptr<AbstractTransport> ZmqBridgeClient::create_binary_transport(
        BinaryTransport<OutputTypeProxy<ZmqBridgeClient *> >  *& in) {

    auto ptr = std::make_unique<BinaryTransport<OutputTypeProxy<ZmqBridgeClient *> > >(
            OutputTypeProxy<ZmqBridgeClient *>{this});
    in = ptr.get();
    return ptr;

}



ZmqBridgeClient::ZmqBridgeClient(Bus bus, zmq::context_t &ctx,
        std::string address, ZmqBridgeConfig config)
    :_endpoint(prepare_socket(ctx, address, config.decorate_socket))
    ,_ping_interval(config.housekeeping_sec/2)
    ,_pool(_endpoint,*this){


    _br.emplace(bus, create_binary_transport(_parser), config.mode);
    _pool.run(config.threads);
    _br->refresh(true);
    _br->send_reset();


}

ZmqBridgeClient::~ZmqBridgeClient() {
}



char* ZmqBridgeClient::output_start(std::size_t sz, MsgFlags ) {
    _mx.lock();
    _out_buffer.clear();
    _out_buffer.resize(sz);
    return _out_buffer.data();
}

DeliveryError ZmqBridgeClient::output_commit(std::size_t sz, MsgFlags ) {
    _out_buffer.resize(sz);
    _endpoint.send({_out_buffer.data(), _out_buffer.size()},{});
    _mx.unlock();
    return DeliveryError::not_used;
}


void ZmqBridgeClient::send_ping() {
    _endpoint.send({},{});
}

void ZmqBridgeClient::on_message(std::string_view data,
        std::string_view ) {
    _parser->parse(data);
}

std::chrono::system_clock::time_point ZmqBridgeClient::on_timeout() {
    send_ping();
    return std::chrono::system_clock::now() + std::chrono::seconds(_ping_interval);
}

void ZmqBridgeClient::on_error(std::string_view ) {
    //do nothing
}

}
