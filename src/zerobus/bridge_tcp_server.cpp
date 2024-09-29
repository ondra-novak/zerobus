#include "bridge_tcp_server.h"

#include "bridge.h"
namespace zerobus {

BridgeTCPServer::BridgeTCPServer(Bus bus, std::shared_ptr<INetContext> ctx, std::string address_port, AuthConfig auth_cfg)
        :_bus(bus),_ctx(std::move(ctx)),_auth_cfg(std::move(auth_cfg)) {
        _aux = _ctx->create_server(address_port);
        auto br = IBridgeAPI::from_bus(_bus.get_handle());
        br->register_monitor(this);
        _next_ping = std::chrono::system_clock::now()+std::chrono::seconds(_ping_interval);
        _ctx->set_timeout(_next_ping, this);
    }

BridgeTCPServer::BridgeTCPServer(Bus bus, std::string address_port, AuthConfig auth_cfg)
    :BridgeTCPServer(std::move(bus), make_context(1), std::move(address_port), std::move(auth_cfg)) {
}

BridgeTCPServer::~BridgeTCPServer() {
    auto br = IBridgeAPI::from_bus(_bus.get_handle());
    br->unregister_monitor(this);
    _ctx->destroy(this);
}


void BridgeTCPServer::on_channels_update() noexcept {
    _ctx->set_timeout(std::chrono::system_clock::time_point::min(), this);
}

bool BridgeTCPServer::on_message_dropped(IListener *, const Message &) noexcept {return false;}

void BridgeTCPServer::on_accept(NetContextAux *aux, std::string /*peer_addr*/) {
    //TODO report peer_addr
    std::lock_guard _(_mx);
    auto p = std::make_unique<Peer>(*this, aux, _id_cntr++);
    _peers.push_back(std::move(p));
}

NetContextAux* BridgeTCPServer::get_context_aux() {
    return _aux;
}

void BridgeTCPServer::on_timeout() {
    auto now  = std::chrono::system_clock::now();
    std::vector<std::shared_ptr<Peer> > pcpy;
    {
        std::lock_guard _(_mx);
        if (now >= _next_ping) {
            _next_ping = now + std::chrono::seconds(_ping_interval);
            _peers.erase(std::remove_if(
                    _peers.begin(), _peers.end(), [&](const auto &p) {
                Peer &x = *p;
                return x.check_dead();
            }),_peers.end());
        }
        for (const auto &x: pcpy) {
            if (!x->disabled()) {
                x->send_mine_channels();
            }
        }
    }
    _ctx->set_timeout(_next_ping, this);
}

BridgeTCPServer::Peer::Peer(BridgeTCPServer &owner, NetContextAux *aux, unsigned int id)
    :BridgeTCPCommon(owner._bus, owner._ctx, aux)
    ,_id(id)
    ,_owner(owner)
    {
    if (owner._auth_cfg.verify_fn) {
        Peer::request_auth(owner._auth_cfg.digest_type);
    } else {
        initial_handshake();
    }
}

void BridgeTCPServer::Peer::initial_handshake() {
    Peer::send_welcome();
}

void BridgeTCPServer::Peer::on_auth_response(std::string_view ident, std::string_view proof, std::string_view salt) {
    _owner.on_auth_response(this, ident, proof, salt);
}

bool BridgeTCPServer::Peer::check_dead() {
    if (_activity_check) {
        if (_ping_sent) return true;
        send_ping();
        _ping_sent = true;
    } else {
        _ping_sent = false;
    }
    _activity_check = true;
    return false;
}

void BridgeTCPServer::Peer::lost_connection() {
    _owner.lost_connection(this);
}

template<typename Fn>
void BridgeTCPServer::call_with_peer(unsigned int id, Fn &&fn) {
    std::lock_guard _(_mx);
    auto iter = std::find_if(_peers.begin(), _peers.end(), [&](const auto &x) {
        return x->get_id() == id;
    });
    if (iter != _peers.end()) {
        fn(iter->get());
    }
}

void BridgeTCPServer::on_auth_response(Peer *p, std::string_view ident, std::string_view proof, std::string_view salt) {
    _auth_cfg.verify_fn(this, AuthInfo{p->get_id(),ident,proof,salt});
}

void BridgeTCPServer::accept_auth(unsigned int id) {
    call_with_peer(id, [&](Peer *p){
        p->initial_handshake();
    });
}

void BridgeTCPServer::accept_auth(unsigned int id, ChannelFilter flt) {
    call_with_peer(id, [&](Peer *p){
        p->set_filter(std::move(flt));
        p->initial_handshake();
    });
}

void BridgeTCPServer::reject_auth(unsigned int id) {
    call_with_peer(id, [&](Peer *p){
        p->send_auth_failed();
    });
}

void BridgeTCPServer::lost_connection(Peer *p) {
    std::lock_guard _(_mx);
    auto ne = std::remove_if(_peers.begin(), _peers.end(), [&](const auto &x){
        return x.get() == p;
    });
    _peers.erase(ne, _peers.end());
}

void BridgeTCPServer::Peer::on_read_complete(std::string_view data) {
    _activity_check = false;
    BridgeTCPCommon::on_read_complete(data);
}

}
