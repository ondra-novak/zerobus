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
    ,_ping_interval(config.housekeeping_sec/2) {

    _br.emplace(bus, create_binary_transport(_parser), config.mode);
    _mthr.add_threads(config.threads, [this](std::stop_token stp, const bool &kf){
       std::stop_callback _(stp,[this]{
           _endpoint.stop();
       });
       worker(kf);
    });


}

ZmqBridgeClient::~ZmqBridgeClient() {
}

void ZmqBridgeClient::worker(const bool &kf) {
    ZmqEndpoint::Message msg;


    while (!_endpoint.is_stopped()) {
        auto tm = std::chrono::system_clock::now() + std::chrono::seconds(_ping_interval);
        auto r = _endpoint.receive(msg, tm);
        switch (r) {
            case ZmqEndpoint::RecStatus::message:
                message_received(msg.get_data());
                if (kf) return;
                break;
            case ZmqEndpoint::RecStatus::timeout:
                send_ping();
                break;
            case ZmqEndpoint::RecStatus::stop_signal:
                return;
            case ZmqEndpoint::RecStatus::error_send:
                //do nothing
                break;
        }
    }

}


char* ZmqBridgeClient::output_start(std::size_t sz) {
    _mx.lock();
    _out_buffer.clear();
    _out_buffer.resize(sz);
    return _out_buffer.data();
}

void ZmqBridgeClient::output_commit(std::size_t sz) {
    _out_buffer.resize(sz);
    _endpoint.send({_out_buffer.data(), _out_buffer.size()},{});
    _mx.unlock();
}

void ZmqBridgeClient::message_received(std::string_view msg) {
    _parser->parse(msg);
}

void ZmqBridgeClient::send_ping() {
    _endpoint.send({},{});
}

}
