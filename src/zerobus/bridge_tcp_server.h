#pragma once
#include "bus.h"
#include "monitor.h"
#include "websocket.h"
#include "http_server.h"

#include "bridge_tcp_common.h"

#include <functional>
namespace zerobus {

class BridgeTCPServer: public IMonitor, public IServer {
public:

    ///Construct server
    /**
     * @param bus local end of the bus
     * @param ctx network context
     * @param address_port address and port to bind. You can use * to bind on all interfaces (*:port)
     * @param auth_cfg optional - information about peer authentication.
     */
    BridgeTCPServer(Bus bus, std::shared_ptr<INetContext> ctx, std::string address_port);
    ///Construct server
    /**
     * @param bus local end of the bus
     * @param address_port address and port to bind. You can use * to bind on all interfaces (*:port)
     * @param auth_cfg optional - information about peer authentication.
     *
     * @note creates network context with single thread
     */
    BridgeTCPServer(Bus bus, std::string address_port);
    ///Construct bridge, but don't create server
    /**
     * The server must be created manually by calling function bind()
     * @param bus bus to connect
     */
    BridgeTCPServer(Bus bus);

    BridgeTCPServer(const BridgeTCPServer &) = delete;
    BridgeTCPServer &operator=(const BridgeTCPServer &) = delete;
    virtual ~BridgeTCPServer() override;

    ///Bind the instance to existing bus and create server
    /**
     * @param ctx network context
     * @param address_port address and port or ws:// string
     */
    void bind(std::shared_ptr<INetContext> ctx, std::string address_port);


    ///sets http server
    /** This allows to process non-websocket requests by some http server.
     *
     * @param srv instance to http server, which processes the non-websocket
     * requests. The pointer is owned by this object and automatically destroyed
     *
     * @note The function is MT safe if called for very first time. If you use
     * this function to switch the server, the old instance is stored to the
     * variable srv. You should consider that the instance can be still in use.
     */
    void set_http_server(std::unique_ptr<IHttpServer> &srv);
    ///sets http server
    /** This allows to process non-websocket requests by some http server.
     *
     * @param srv instance to http server, which processes the non-websocket
     * requests. The pointer is owned by this object and automatically destroyed
     *
     * @note The function is MT safe if called for very first time. If you use
     * this function to switch the server, the old instance is stored to the
     * variable srv. You should consider that the instance can be still in use.
     */
    void set_http_server(std::unique_ptr<IHttpServer> &&srv) {
        return set_http_server(srv);
    }

    ///set http server as function called for every non-websocket request
    /**
     *
     * @param fn function to be called
     * @return pointer for previous handler
     * @see set_http_server, IHttpServer
     */
    template<std::invocable<ConnHandle,std::shared_ptr<INetContext> ,std::string_view , std::string_view> Fn>
    std::unique_ptr<IHttpServer> set_http_server_fn(Fn &&fn);

    ///server doesn't send pings automatically - this function enforces ping on all peers
    /**
     * You should call this function repeatedly in steady interval, for example 1 minute.
     * However it is not recommended to implement short ping. Removing stall connections
     * can cause loosing of messages in case of temporary break of a connection (for example
     * lost signal or disconnected cable). Connection is kept active even if there is
     * no activity.
     *
     * @note browser's websocket still can send and receive pings.
     */
    void send_ping();

    ///set high water mark
    /**
     * @param hwm specified high water mark limit for total buffered data in bytes. Default is
     *  1 MB
     * @param timeout_ms specifies timeout how long is sending blocked if high water mark
     *    limit is reached. This blocking is synchronous. If timeout is reached, the
     *    message is dropped (and lost). You can specify some small timeout to slow down
     *    sending in case that data are generated faster than is speed of the connection.
     *    Default is 1 second
     */
    void set_hwm(std::size_t hwm, std::size_t timeout_ms);

    void set_session_timeout(std::size_t timeout_sec);

protected:

    ///called when peer is connected
    /**
     * @param p connected peer, it should be in state after initial handshake
     */
    virtual void on_peer_connect(BridgeTCPCommon &p);
    ///called when peer is lost
    /**
     * @param p lost peer, it should be in state before its destruction
     */
    virtual void on_peer_lost(BridgeTCPCommon &p);

    virtual void on_channels_update() noexcept override;
    virtual void on_accept(ConnHandle aux, std::string peer_addr) noexcept override;
    virtual void on_timeout() noexcept override;

    class Peer : public BridgeTCPCommon {
    public:

        Peer(BridgeTCPServer &owner, ConnHandle aux, unsigned int id);
        Peer(const Peer &) = delete;
        Peer &operator=(const Peer &) = delete;
        ~Peer();
        void initial_handshake();
        virtual void receive_complete(std::string_view data) noexcept override;
        virtual void lost_connection() override;
        virtual void on_timeout() noexcept override;

        bool check_dead();
        unsigned int get_id() const {return _id;}
        std::string_view get_session_id() const {return _session_id;}
        bool is_lost() const {return _lost;}
        bool disabled() const {return  _handshake;}
        virtual void close() override;
        void reconnect(ConnHandle aux);

    protected:
        bool _activity_check = false;
        bool _ping_sent = false;
        bool _lost = false;
        unsigned int _id;
        std::string _session_id;

        struct ParseResult {
            std::string_view key;
            std::string_view uri;
            std::string_view method;
            std::string_view sessionid;
        };

        bool websocket_handshake(const std::string_view &data, const std::string_view &extra);
        ParseResult parse_websocket_header(std::string_view data);
        void start_peer();

    private:
        BridgeTCPServer &_owner;
    };


    Bus _bus;
    std::shared_ptr<INetContext> _ctx;
    ConnHandle  _aux = 0;
    std::string _path;
    std::mutex _mx;
    std::vector<std::unique_ptr<Peer> > _peers;
    std::chrono::system_clock::time_point _next_ping = {};
    std::size_t _hwm = 1024*1024;
    std::size_t _hwm_timeout = 1000;    //1 second
    std::size_t _session_timeout = 0;
    unsigned int _id_cntr = 1;
    bool _send_mine_channels_flag = false;
    bool _lost_peers_flag = false;
    bool _bound = false;
    std::atomic<IHttpServer *> _http_server = {};


    void lost_connection();
    ///try to handover the session
    /**
     * @param handle connection handle
     * @param session_id session id
     * @retval true connection has been handed over to original peer instance. You should close this
     * peer
     * @retval false connection has not been handed over, continue in this peer
     */
    bool handover(Peer *peer, ConnHandle handle, std::string_view session_id);


    template<typename Fn>
    void call_with_peer(unsigned int id, Fn &&fn);
    struct AuthCBDeleter;
};

template<std::invocable<ConnHandle, std::shared_ptr<INetContext>, std::string_view, std::string_view> Fn>
std::unique_ptr<IHttpServer> BridgeTCPServer::set_http_server_fn(Fn &&fn) {
    class Impl: public IHttpServer {
    public:
        Impl(Fn &&f):_fn(std::forward<Fn>(f)) {}
        virtual void on_request( ConnHandle handle,
                std::shared_ptr<INetContext> ctx,
                std::string_view header, std::string_view body_data) noexcept override {
            _fn(handle, std::move(ctx), header, body_data);
        }
    protected:
        std::decay_t<Fn> _fn;
    };
    auto ptr = std::unique_ptr<IHttpServer>(new Impl(std::forward<Fn>(fn)));
    set_http_server(ptr);
    return ptr;
}

}
