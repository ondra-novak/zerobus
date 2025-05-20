#include "ws_bridge_server.hpp"

#include "../../binary_transport.hpp"

namespace zerobus {


WsBridgeServer::WsBridgeServer(Bus bus,
        std::string address,
        WsBridgeConfig config)

        :_bus(std::move(bus))
        ,_mode(config.mode)
        ,_housekeeping_sec(config.housekeeping_sec)
{
    _endpoint.bind(address);
    _pool.add_threads(config.threads,[this](std::stop_token stp,  const bool &kf){
        worker(stp, kf);
    });


}

WsBridgeServer::~WsBridgeServer() {

}


void WsBridgeServer::do_housekeeping() {

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

void WsBridgeServer::sleep_peer(std::string_view identity, bool sleep) {
    std::shared_lock _(_mx);
    auto iter = _peers.find(identity);
    if (iter == _peers.end()) return;
    iter->second->set_sleeping(sleep);
}

std::unique_ptr<AbstractTransport> WsBridgeServer::create_binary_transport(
        PeerContext *out,
        BinaryTransport<OutputTypeProxy<PeerContext *> >  *& in) {

    auto ptr = std::make_unique<BinaryTransport<OutputTypeProxy<PeerContext *> > >(
            OutputTypeProxy<PeerContext *>{out});
    in = ptr.get();
    return ptr;
}



WsBridgeServer::PeerContext::PeerContext(WsBridgeServer &owner, std::string_view ident)
    :_owner(owner)
    ,_ident(ident)
    ,_last_activity(std::chrono::system_clock::now())
{
    _br.emplace(_owner._bus, create_binary_transport(this, _parser), _owner._mode);
}

char* WsBridgeServer::PeerContext::output_start(std::size_t sz) {
    _mx.lock();
    _out_buffer.resize(sz);
    return _out_buffer.data();
}

void WsBridgeServer::PeerContext::output_commit(std::size_t sz) {
    _out_buffer.resize(sz);
     if (_sleeping) {
         _q.push(std::string(_out_buffer.begin(), _out_buffer.end()));
     } else {
        _last_activity = std::chrono::system_clock::now();
        if (!_owner._endpoint.send_message(
                {_out_buffer.begin(), _out_buffer.end()}, _h)) {
            _sleeping = true;
            _q.push(std::string(_out_buffer.begin(), _out_buffer.end()));
                }
     }
     _mx.unlock();
}

void WsBridgeServer::PeerContext::on_incoming_message(std::string_view msg, WsEndpoint::Handle slot) {
    _h = slot;
    _parser->parse(msg);
    set_sleeping(false);
}


void WsBridgeServer::PeerContext::set_sleeping(bool sleeping) {
    std::lock_guard _(_mx);
    _sleeping = sleeping;
    while (!_sleeping && !_q.empty()) {
        if (!_owner._endpoint.send_message(_q.front(), _h)) {
            _sleeping = true;
        } else {
            _q.pop();
        }
    }
    if (!_sleeping) {
        _last_activity = std::chrono::system_clock::now();
    }
}

std::chrono::system_clock::time_point WsBridgeServer::PeerContext::get_last_activity() const {
    std::lock_guard _(_mx);
    return _last_activity;
}

void WsBridgeServer::on_message(std::string_view data, std::string_view identity, WsEndpoint::Handle h) {
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
    iter->second->on_incoming_message(data,h);
}

std::chrono::system_clock::time_point WsBridgeServer::on_timeout() {
    auto nx = std::chrono::system_clock::now()+std::chrono::seconds(_housekeeping_sec);
    do_housekeeping();
    return nx;
}

void WsBridgeServer::on_error(std::string_view ident) {
    sleep_peer(ident, true);
}

void WsBridgeServer::worker(std::stop_token stp, const bool &kf) {
    WsEndpoint::Message msg;
    auto tm = _next_housekeeping.exchange(std::chrono::system_clock::time_point::max());
    while (!stp.stop_requested()) {
        WsEndpoint::RecStatus rs = _endpoint.receive(msg, tm);
        if (rs == WsEndpoint::RecStatus::message) {
            on_message(msg.data, msg.ident, msg.prev_handle);
        } else if (rs == WsEndpoint::RecStatus::timeout) {
            tm = on_timeout();
        } else if (rs == WsEndpoint::RecStatus::pong) {
            on_message("", msg.ident, msg.prev_handle);
        }
        if (kf) return;

    }
}

}
