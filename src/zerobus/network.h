#pragma once

#include <span>
#include <string_view>
#include <memory>
#include <chrono>

namespace zerobus {



class IPeerServerCommon;
class IPeer;
class IServer;

///Identification of connection (server socket)
using ConnHandle = unsigned int;

class INetContext {
public:


    virtual ~INetContext() = default;

    ///connects peer to an address
    /**
     * @param address_port address:port of target
     * @return if connection is successful (still pending to connect but valid),
     * return handle to the connection.
     * @exception std::system_error when connection cannot be established
     *
     @note you should create IPeer for the result
     *
     */
    virtual ConnHandle peer_connect(std::string address_port) = 0;

    ///creates server
    /**
     * @param address_port address:port where open port
     * @return if connection is successful (still pending to connect but valid),
     * return handle to the connection.
     * @exception std::system_error when connection cannot be established
     *
     * @note you should create IServer for the result
     */
    virtual ConnHandle create_server(std::string address_port) = 0;

    ///closes connection and connects it again to address
    /**
     * replaces connection identified by the handle
     *
     * @param connection current connection handle
     * @param address_port new address:port
     * @exception std::system_error when connection cannot be established. In this
     * case old connection is not replaces
     *
     * @note replacing the connection causes canceling all pending callbacks (expect
     * timeout).
     */
    virtual void reconnect(ConnHandle connection, std::string address_port) = 0;
    ///start receiving data
    /**
     * @param connection connection handle
     * @param buffer reference to a character buffer (preallocated)
     * @param peer pointer to peer, which receives callback, when receive is successful
     */
    virtual void receive(ConnHandle connection, std::span<char> buffer, IPeer *peer) = 0;
    ///Send a data
    /**
     * @param connection connection handle
     * @param data data to send. Sending empty string causes sending EOF, which
     * closes connection.
     * @return count of bytes written. The function can return 0, which can mean
     * that connection has been reset or output buffer is full. To detect connection
     * reset, if zero is returned after clear_to_send(), it does mean, that
     * connection has been reset. Otherwise, output buffer is probably full and you
     * need to requst callback_on_send_available
     */
    virtual std::size_t send(ConnHandle connection, std::string_view data) = 0;
    ///notifies context that peer is ready to send data
    /**
     * Result of this call is calling function clear_to_send(), when send is possible.
     * Then the peer can use send() function to send data.
     *
     * There is no specified interval between clear_to_send() and invocation of send.
     * You can use ready_to_send() , receive clear_to_send() to ensure, that
     * next send() will able to send a data. You can call ready_to_send many time you
     * need.
     *
     * @param connection connection handle
     * @param peer the peer which receives callback
     */
    virtual void ready_to_send(ConnHandle connection, IPeer *peer) = 0;


    ///request to accept next connection
    /**
     * @param connection connection handle
     * @param server server which receives callback
     */
    virtual void accept(ConnHandle connection, IServer *server) = 0;

    ///request to destroy server/peer
    /**
     * You must call this function to close the connection.
     * @param connection connection handle
     *
     * @note the function blocks, if there are active callbacks. You must finish
     * all callbacks to finish this functions
     */
    virtual void destroy(ConnHandle connection) = 0;

    ///sets timeout at given time
    /**
     * @param tp timeout point
     * @param p peer or server object
     *
     * If a timeout is set, it works as clear_timeout + set_timeout
     */
    virtual void set_timeout(ConnHandle connection, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) = 0;

    ///Clear existing timeout
    virtual void clear_timeout(ConnHandle connection) = 0;
};

class IPeerServerCommon {
public:
    virtual ~IPeerServerCommon() = default;
    ///called when time specified by set_timeout() is reached
    virtual void on_timeout() noexcept= 0;
};

class IPeer: public IPeerServerCommon {
public:
    virtual ~IPeer() = default;

    ///called by context, if send is available
    /**
     * When this function called, you can call send() which will be able to send
     * a data to the connection. You should then use ready_to_send() if you
     * need to write more data.
     */
    virtual void clear_to_send() noexcept= 0;

    ///called by context, if receive is complete,
    /**
     * @param data received data. If the string is empty, then connection has been closed
     */
    virtual void receive_complete(std::string_view data) noexcept= 0;
};

class IServer: public IPeerServerCommon {
public:
    virtual ~IServer() = default;

    ///called when new connection is accepted
    /**
     * You need to create peer and set the returned value as aux
     */
    virtual void on_accept(ConnHandle connection, std::string peer_addr) noexcept= 0;
};

std::shared_ptr<INetContext> make_context(int iothreads);


///creates server which calls a user callback with data required to create a new peer
template<std::invocable<ConnHandle, std::string> CB>
class ServerCallback: public IServer {
public:
    ServerCallback(std::shared_ptr<INetContext> ctx, ConnHandle connection, CB &&cb)
        :_ctx(std::move(ctx)), _connection(connection), _cb(std::forward<CB>(cb)) {}

    ~ServerCallback() {
        _ctx->destroy(this);
    }

    ServerCallback(const ServerCallback &) = delete;
    ServerCallback &operator=(const ServerCallback &) = delete;

    virtual void on_accept(ConnHandle connection, std::string peer_addr) noexcept override {
        _cb(connection, std::move(peer_addr));
    }
    virtual void on_timeout() noexcept {}


protected:
    std::shared_ptr<INetContext> _ctx;
    ConnHandle _connection;
    CB _cb;
};

///Makes server
/**
 * @param ctx a network context
 * @param address_port local address and port. Use * for any address. Use * for random port
 * @param cb
 * @return
 */
template<std::invocable<ConnHandle, std::string> CB>
auto make_server(std::shared_ptr<INetContext> ctx, std::string address_port, CB &&cb) {
    using AdjCB = std::decay_t<CB>;
    auto aux = ctx->create_server(std::move(address_port));
    return ServerCallback<AdjCB>(std::move(ctx), aux, std::forward<CB>(cb));
}

}
