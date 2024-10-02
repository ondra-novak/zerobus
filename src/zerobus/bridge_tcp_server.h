#pragma once
#include "bus.h"
#include "monitor.h"
#include "websocket.h"

#include "bridge_tcp_common.h"
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

    static constexpr unsigned int ping_interval_sec_default = 60;

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
    void accept_auth(unsigned int id, ChannelFilter flt);
    ///Reject authentication for waiting peer
    /**
     * @param id id of peer. This id is available at the structure AuthInfo
     */
    void reject_auth(unsigned int id);

protected:

    virtual void on_channels_update() noexcept override;
    virtual bool on_message_dropped(IListener *lsn, const Message &msg) noexcept override;
    virtual void on_accept(SocketIdent aux, std::string peer_addr) noexcept override;
    virtual void on_timeout() noexcept override;

    class Peer : public BridgeTCPCommon {
    public:
        enum class Format {
            unknown, zbus, websocket
        };

        Peer(BridgeTCPServer &owner, SocketIdent aux, unsigned int id);
        Peer(const Peer &) = delete;
        Peer &operator=(const Peer &) = delete;
        virtual void on_auth_response(std::string_view ident, std::string_view proof, std::string_view salt) override;
        void initial_handshake();
        virtual void on_read_complete(std::string_view data) noexcept override;
        virtual void lost_connection() override;

        bool check_dead();
        unsigned int get_id() const {return _id;}

        virtual void output_message(std::string_view message) override;

    protected:
        Format _format = Format::unknown;
        bool _activity_check = false;
        bool _ping_sent = false;
        unsigned int _id;

        bool websocket_handshake(std::string_view &data);
        bool parse_websocket_stream(std::string_view data);
        void send_websocket_message(std::string_view data);
        void send_websocket_message(const ws::Message &msg);
        void start_peer();

    private:
        BridgeTCPServer &_owner;
        ws::Parser _ws_parser;
    };


    Bus _bus;
    std::shared_ptr<INetContext> _ctx;
    SocketIdent  _aux = 0;
    AuthConfig _auth_cfg;
    std::mutex _mx;
    std::vector<std::unique_ptr<Peer> > _peers;
    std::chrono::system_clock::time_point _next_ping = {};
    unsigned int _id_cntr = 1;

    unsigned int _ping_interval = ping_interval_sec_default;

    void on_auth_response(Peer *p, std::string_view ident, std::string_view proof, std::string_view salt);
    void lost_connection(Peer *p);

    template<typename Fn>
    void call_with_peer(unsigned int id, Fn &&fn);
    struct AuthCBDeleter;
};


}
