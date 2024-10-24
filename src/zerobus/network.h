#pragma once

#include <span>
#include <string_view>
#include <memory>
#include <chrono>
#include <functional>
#include <source_location>
#include <stop_token>

namespace zerobus {



class IPeerServerCommon;
class IPeer;
class IServer;

///Identification of connection (server socket)
using ConnHandle = unsigned int;

enum class SpecialConnection {
    /** not actual connection - you can use for on_timer feature */
    null,
    ///connect stdin
    stdinput,
    ///connect stdout
    stdoutput,
    ///connect stderr
    stderror,
    ///associate a file descriptor (named pipe)
    descriptor,
    ///associate an already initialized socket
    socket
};


struct PipePair {
    ConnHandle read;
    ConnHandle write;
};

class SimpleAction { // @suppress("Miss copy constructor or assignment operator")
public:
    static constexpr std::size_t _max_lambda_size = sizeof(void *) * 7;
    template<std::invocable<> Fn>
    SimpleAction(Fn &&fn) {
        using TFn = std::decay_t<Fn>;
        static_assert(sizeof(TFn) <= _max_lambda_size && std::is_trivially_copy_constructible_v<TFn>);
        TFn *tfn = reinterpret_cast<TFn *>(_space);
        std::construct_at(tfn, std::forward<Fn>(fn));
        _run_fn = [](SimpleAction *me) {
            TFn *tfn = reinterpret_cast<TFn *>(me->_space);
            (*tfn)();
        };
    }

    void operator()() {
        _run_fn(this);
    }

protected:
    void (* _run_fn)(SimpleAction *) = {};
    char _space[_max_lambda_size] = {};

};

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
    virtual ConnHandle connect(std::string address_port) = 0;

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

    ///create pipe
    /**
    * @return initialized pipe. Note the pipe is single direction. So you can only read
    * the read-end and only write the write-end
    */
    virtual PipePair create_pipe() = 0;


    ///Make special connection
    /**
     * @param type type of special connection
     * @param arg an argument for type `descriptor` or `socket`.
     * Because descriptor is OS specific object. For Linux platform,
     * there is pointer to int as posix file descriptor. For Windows, the
     * function accepts pointer to HANDLE. If type `socket` the it is accepted
     * int for linux or SOCKET for windows
     *
     * @return connection handle
     *
     * @note the function actually duplicates the descriptor after use
     *
     * @note on Windows platform, Socket cannot be duplicated. Do not close the socket after use.
     *
     * @note on Windows platform, HANDLE (as descriptor) must be created with FILE_FLAG_OVERLAPPED
     * and must be suitable for IOCP. HANDLE is internally duplicated, so you can close handle
     * after use
     *
     */
    virtual ConnHandle connect(SpecialConnection type, const void *arg = nullptr) = 0;


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
     *
     * If you need to destroy from callback, you need to use enqueue() to move
     * execution out of callback.
     */
    virtual void destroy(ConnHandle connection) = 0;

    ///Enqueue an operation to dispatcher's thread
    /** Enqueued operation is not considered as callback call, so it
     * is good context to perform destructions
     *
     * @param fn function to enqueue.
     *
     * @note the function must contain minimal closure which also needs to
     * be trivially copy constructible.
     *
     * On other hand, if called from a handler, it guarantee that callback function
     * will be called, so you  don't need any "guards"
     */
    virtual void enqueue(SimpleAction fn) = 0;


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

    ///Determines, whether current execution is made in a callback
    /**
     * @retval true we are currently in callback, we cannot destroy the connection
     * @retval false we are outside of a callback
     */
    virtual bool in_calback() const = 0;
};

class IPeerServerCommon {
public:
    virtual ~IPeerServerCommon() = default;
    ///called when time specified by set_timeout() is reached
    /**
     * @note this call is marked as callback function. You cannot call destroy() from it
     */
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
     *
     * @note this call is marked as callback function. You cannot call destroy() from it
     */
    virtual void clear_to_send() noexcept= 0;

    ///called by context, if receive is complete,
    /**
     * @param data received data. If the string is empty, then connection has been closed
     *
     * @note this call is marked as callback function. You cannot call destroy() from it
     *
     */
    virtual void receive_complete(std::string_view data) noexcept= 0;
};

class IServer: public IPeerServerCommon {
public:
    virtual ~IServer() = default;

    ///called when new connection is accepted
    /**
     * You need to create peer and set the returned value as aux
     *
     * @note this call is marked as callback function. You cannot call destroy() from it
     */
    virtual void on_accept(ConnHandle connection, std::string peer_addr) noexcept= 0;
};

/// @brief Called on error in network dispatcher - for logging purpose
/**
 * @param string contains name of operation during error reported
 * @param source_location location of error in library source code
 *
 * @note The actual error description is carried as a current exception. Use std::current_exception() to
 * retrieve exception to analyze what happened
 */
using ErrorCallback = std::function<void(std::string_view, std::source_location)>;


std::shared_ptr<INetContext> make_network_context(int iothreads = 1);
std::shared_ptr<INetContext> make_network_context(ErrorCallback errcb, int iothreads = 1);


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

///spawn process and create pipe connection with stdin and stdout
/**
 * @param ctx network context shared pointer
 * @param command_line command line. It must contain program name and arguments.
 *                    Double quotes are supported.
 * @param tkn (optional) stop token. Allows to stop process when requested.
 * @param exit_action (optional) specify action called when process exited.
 * @return pair of connection handles for read and write. You probably want to
 * construct BridgePipe
 */
PipePair spawn_process(std::shared_ptr<INetContext> ctx,
                        std::string_view command_line,
                        std::stop_token tkn = {},
                        std::function<void(int)> exit_action = {});

}
