#include "zmq_bridge_server.hpp"

#include "../../binary_transport.hpp"

namespace zerobus {

static zmq::socket_t prepare_socket(
        zmq::context_t &ctx,
        const std::string &address,
        const std::function<void(zmq::socket_t &)> &decorate) {

    zmq::socket_t out(ctx, zmq::socket_type::router);
    if (decorate) decorate(out);
    out.set(zmq::sockopt::router_mandatory, 1);
    out.bind(address);
    return out;

}

ZmqBridgeServer::ZmqBridgeServer(Bus bus,
        zmq::context_t &ctx,
        std::string address,
        ZmqBridgeConfig config)

        :_bus(std::move(bus))
        ,_endpoint(prepare_socket(ctx, address, config.decorate_socket))
        ,_mode(config.mode)
        ,_housekeeping_sec(config.housekeeping_sec)
        ,_pool(_endpoint, *this)
{

    _pool.run(config.threads);
}

ZmqBridgeServer::~ZmqBridgeServer() {
}


void ZmqBridgeServer::do_housekeeping() {

    std::vector<std::unique_ptr<PeerContext> > _out_ctx;
    std::lock_guard _(_mx);
    auto now = std::chrono::system_clock::now();
    auto dur = std::chrono::seconds(_housekeeping_sec);
    for (auto iter = _peers.begin(); iter != _peers.end();) {
        if (iter->second->get_last_activity()+dur < now) {
            _out_ctx.push_back(std::move(iter->second));
            iter = _peers.erase(iter);
        } else {
            ++iter;
        }
    }
}

void ZmqBridgeServer::sleep_peer(std::string_view identity, bool sleep) {
    std::shared_lock _(_mx);
    auto iter = _peers.find(identity);
    if (iter == _peers.end()) return;
    iter->second->set_sleeping(sleep);
}

std::unique_ptr<AbstractTransport> ZmqBridgeServer::create_binary_transport(
        PeerContext *out,
        BinaryTransport<OutputTypeProxy<PeerContext *> >  *& in) {

    auto ptr = std::make_unique<BinaryTransport<OutputTypeProxy<PeerContext *> > >(
            OutputTypeProxy<PeerContext *>{out});
    in = ptr.get();
    return ptr;
}



ZmqBridgeServer::PeerContext::PeerContext(ZmqBridgeServer &owner, std::string_view ident)
    :_owner(owner)
    ,_ident(ident)
    ,_last_activity(std::chrono::system_clock::now())
{
    _br.emplace(_owner._bus, create_binary_transport(this, _parser), _owner._mode);
}

char* ZmqBridgeServer::PeerContext::output_start(std::size_t sz) {
    _mx.lock();
    _out_buffer.resize(sz);
    return _out_buffer.data();
}

void ZmqBridgeServer::PeerContext::output_commit(std::size_t sz) {
    _out_buffer.resize(sz);
     if (_sleeping) {
         _q.push(std::string(_out_buffer.begin(), _out_buffer.end()));
     } else {
        _last_activity = std::chrono::system_clock::now();
        _owner._endpoint.send({_out_buffer.begin(), _out_buffer.end()}, _ident);
     }
     _mx.unlock();
}

void ZmqBridgeServer::PeerContext::on_incoming_message(std::string_view msg) {
    _parser->parse(msg);
    set_sleeping(false);
}


void ZmqBridgeServer::PeerContext::set_sleeping(bool sleeping) {
    std::lock_guard _(_mx);
    _sleeping = sleeping;
    while (!_sleeping && !_q.empty()) {
        _owner._endpoint.send(_q.front(), _ident);
        _q.pop();
    }
    if (!_sleeping) {
        _last_activity = std::chrono::system_clock::now();
    }
}

std::chrono::system_clock::time_point ZmqBridgeServer::PeerContext::get_last_activity() const {
    std::lock_guard _(_mx);
    return _last_activity;
}

void ZmqBridgeServer::on_message(std::string_view data, std::string_view identity) {
    std::shared_lock lk(_mx);
    auto iter = _peers.find(identity);
    while (iter == _peers.end()) {
        lk.unlock();
        {
            std::unique_lock _(_mx);
            auto p = std::make_unique<PeerContext>(*this, identity);
            _peers.emplace(p->get_ident(), std::move(p));
        }
        lk.lock();
        iter = _peers.find(identity);
    }
    iter->second->set_sleeping(false);
    iter->second->on_incoming_message(data);
}

std::chrono::system_clock::time_point ZmqBridgeServer::on_timeout() {
    auto nx = std::chrono::system_clock::now()+std::chrono::seconds(_housekeeping_sec);
    do_housekeeping();
    return nx;
}

void ZmqBridgeServer::on_error(std::string_view ident) {
    sleep_peer(ident, true);
}

}
