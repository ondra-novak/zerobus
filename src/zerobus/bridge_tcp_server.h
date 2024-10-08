#pragma once
#include "bus.h"
#include "monitor.h"
#include "websocket.h"

#include "bridge_tcp_common.h"

#include <queue>
namespace zerobus {

class BridgeTCPServer: public IMonitor, public IServer {
public:

    struct AuthInfo {
        ///ID of associated peer
        /**
         * You can use this ID with accept_auth or reject_auth
         */
        unsigned int peer_id;
        ///identity
        std::string_view ident;
        ///proof of identity
        std::string_view proof;
        ///salt used to encrypt the proof
        std::string_view salt;
    };


    ///configures authentication check procedure
    struct AuthConfig {
        ///specifies required digest type
        std::string digest_type;
        ///a function called when auth is requested
        /**
         * @param 1 pointer to server instance
         * @param 2 AuthInfo
         */
        std::function<void(BridgeTCPServer *,  AuthInfo)> verify_fn;
    };

    ///Construct server
    /**
     * @param bus local end of the bus
     * @param ctx network context
     * @param address_port address and port to bind. You can use * to bind on all interfaces (*:port)
     * @param auth_cfg optional - information about peer authentication.
     */
    BridgeTCPServer(Bus bus, std::shared_ptr<INetContext> ctx, std::string address_port, AuthConfig auth_cfg = {});
    ///Construct server
    /**
     * @param bus local end of the bus
     * @param address_port address and port to bind. You can use * to bind on all interfaces (*:port)
     * @param auth_cfg optional - information about peer authentication.
     *
     * @note creates network context with single thread
     */
    BridgeTCPServer(Bus bus, std::string address_port, AuthConfig auth_cfg = {});
    BridgeTCPServer(const BridgeTCPServer &) = delete;
    BridgeTCPServer &operator=(const BridgeTCPServer &) = delete;
    virtual ~BridgeTCPServer() override;

    ///Accept authentication for waiting peer
    /**
     * @param id id of peer. This id is available at the structure AuthInfo
     */
    void accept_auth(unsigned int id);
    ///Accept authentication for waiting peer and apply filter
    /**
     * @param id id of peer. This id is available at the structure AuthInfo
     * @param flt channel filter
     */
    void accept_auth(unsigned int id, std::unique_ptr<IChannelFilter> flt);
    ///Reject authentication for waiting peer
    /**
     * @param id id of peer. This id is available at the structure AuthInfo
     */
    void reject_auth(unsigned int id);


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

    void set_hwm(std::size_t sz);

protected:

    virtual void on_channels_update() noexcept override;
    virtual void on_accept(ConnHandle aux, std::string peer_addr) noexcept override;
    virtual void on_timeout() noexcept override;

    class Peer : public BridgeTCPCommon {
    public:

        Peer(BridgeTCPServer &owner, ConnHandle aux, unsigned int id);
        Peer(const Peer &) = delete;
        Peer &operator=(const Peer &) = delete;
        virtual void on_auth_response(std::string_view ident, std::string_view proof, std::string_view salt) override;
        void initial_handshake();
        virtual void receive_complete(std::string_view data) noexcept override;
        virtual void lost_connection() override;

        bool check_dead();
        unsigned int get_id() const {return _id;}
        bool is_lost() const {return _lost;}
        bool disabled() const {return BridgeTCPCommon::disabled() || _handshake;}

    protected:
        bool _activity_check = false;
        bool _ping_sent = false;
        bool _lost = false;
        unsigned int _id;

        struct ParseResult {
            std::string_view key;
            std::string_view uri;
            std::string_view method;
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
    AuthConfig _auth_cfg;
    std::string _path;
    std::mutex _mx;
    std::vector<std::unique_ptr<Peer> > _peers;
    std::chrono::system_clock::time_point _next_ping = {};
    std::size_t _hwm = 1024*1024;
    unsigned int _id_cntr = 1;
    bool _send_mine_channels_flag = false;
    bool _lost_peers_flag = false;
    std::function<CustomPage(std::string_view)> _custom_page;


    void on_auth_response(Peer *p, std::string_view ident, std::string_view proof, std::string_view salt);
    void lost_connection();


    template<typename Fn>
    void call_with_peer(unsigned int id, Fn &&fn);
    struct AuthCBDeleter;
};


}
