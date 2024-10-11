#pragma once
#include "bus.h"
#include "monitor.h"
#include "websocket.h"

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
    BridgeTCPServer(const BridgeTCPServer &) = delete;
    BridgeTCPServer &operator=(const BridgeTCPServer &) = delete;
    virtual ~BridgeTCPServer() override;



    struct CustomPage {
        int status_code;
        std::string status_message;
        std::string content_type;
        std::string content;
    };

    ///sets callback which is called when non-websocket request is received
    /**
     * It can only server simple GET requests
     * @param cb callback. The callback receive uri-path and it should return CustomPage structure
     */
    void set_custom_page_callback(std::function<CustomPage(std::string_view)> cb);

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

        bool websocket_handshake(std::string_view &data);
        ParseResult parse_websocket_header(std::string_view data);
        void start_peer();

    private:
        BridgeTCPServer &_owner;
        ws::Parser _ws_parser;
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
    std::function<CustomPage(std::string_view)> _custom_page;


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


}
