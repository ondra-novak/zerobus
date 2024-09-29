#pragma once

#include <span>
#include <string_view>
#include <memory>
#include <chrono>

namespace zerobus {

using Socket = int;

class IPeerServerCommon;
class IPeer;
class IServer;
class NetContextAux;

class INetContext {
public:


    virtual ~INetContext() = default;

    ///connects peer to an address
    /**
     * @param address_port address and port entered as "address:port"
     * @return pointer to ContextAux. This pointer must be stored within peer and
     * returned every-time the context calls a function IPeer::get_context_aux. The
     * peer also must call destroy() during its destruction
     *
     */
    virtual NetContextAux *peer_connect(std::string address_port) = 0;

    ///creates server
    /** @param address_port address and port entered as "address:port"
    * @return pointer to ContextAux. This pointer must be stored within the server and
    * returned every-time the context calls a function IServer::get_context_aux. The
    * peer also must call destroy() during its destruction
    */
    virtual NetContextAux *create_server(std::string address_port) = 0;

    ///closes connection and connects it again to address
    /**
     * @param peer connected peer
     * @param address_port address and port to reconnect
     *
     * if exception is thrown, the current connection stays untouched
     * If reconnect is successful, pending receive is canceled, you need to start it again.
     * Writing is not allowed you need to use callback_on_send_available
     */
    virtual void reconnect(IPeer *peer, std::string address_port) = 0;
    ///start receiving data
    virtual void receive(std::span<char> buffer, IPeer *peer) = 0;
    ///send data.
    virtual std::size_t send(std::string_view data, IPeer *peer) = 0;
    ///ask context whether send is available
    virtual void callback_on_send_available(IPeer *peer) = 0;


    ///request to accept next connection
    virtual void accept(IServer *server) = 0;

    ///request to destroy server/peer
    virtual void destroy(IPeerServerCommon *server) = 0;

    ///sets timeout at given time
    /**
     * @param tp timeout point
     * @param p peer or server object
     *
     * If a timeout is set, it works as clear_timeout + set_timeout
     */
    virtual void set_timeout(std::chrono::system_clock::time_point tp, IPeerServerCommon *p) = 0;

    ///Clear existing timeout
    virtual void clear_timeout(IPeerServerCommon *p) = 0;
};

class IPeerServerCommon {
public:
    virtual ~IPeerServerCommon() = default;
    ///retrieves aux context data (stored by set_context_data)
    virtual NetContextAux *get_context_aux() = 0;
    ///called when time specified by set_timeout() is reached
    virtual void on_timeout() = 0;
};

class IPeer: public IPeerServerCommon {
public:
    virtual ~IPeer() = default;

    ///called by context, if send is available
    virtual void on_send_available() = 0;

    ///called by context, if read is complete (delivering read data)
    virtual void on_read_complete(std::string_view data) = 0;
};

class IServer: public IPeerServerCommon {
public:
    virtual ~IServer() = default;

    ///called when new connection is accepted
    /**
     * You need to create peer and set the returned value as aux
     */
    virtual void on_accept(NetContextAux * aux, std::string peer_addr) = 0;
};

std::shared_ptr<INetContext> make_context(int iothreads);


///creates server which calls a user callback with data required to create a new peer
template<std::invocable<NetContextAux *, std::string> CB>
class ServerCallback: public IServer {
public:
    ServerCallback(std::shared_ptr<INetContext> ctx, NetContextAux *server_aux, CB &&cb)
        :_ctx(std::move(ctx)), _aux(server_aux), _cb(std::forward<CB>(cb)) {}

    ~ServerCallback() {
        _ctx->destroy(this);
    }

    ServerCallback(const ServerCallback &) = delete;
    ServerCallback &operator=(const ServerCallback &) = delete;

    virtual void on_accept(NetContextAux *aux, std::string peer_addr) override {
        _cb(aux, std::move(peer_addr));
    }
    virtual NetContextAux* get_context_aux() override {
        return _aux;
    }
    virtual void on_timeout() {}


protected:
    std::shared_ptr<INetContext> _ctx;
    NetContextAux *_aux;
    CB _cb;
};

///Makes server
/**
 * @param ctx a network context
 * @param address_port local address and port. Use * for any address. Use * for random port
 * @param cb
 * @return
 */
template<std::invocable<NetContextAux *, std::string> CB>
auto make_server(std::shared_ptr<INetContext> ctx, std::string address_port, CB &&cb) {
    using AdjCB = std::decay_t<CB>;
    auto aux = ctx->create_server(std::move(address_port));
    return ServerCallback<AdjCB>(std::move(ctx), aux, std::forward<CB>(cb));
}

}
