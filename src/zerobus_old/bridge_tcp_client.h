#pragma once

#include "bridge_tcp_common.h"
#include <mutex>
namespace zerobus {

///Implements bridge over TCP
class BridgeTCPClient: public BridgeTCPCommon, public IMonitor {
public:




    ///construct the client
    /**
     * @param bus local end of the bus
     * @param ctx network context
     * @param address server's address:port
     * @param acb optional a callback which is called when authentication is requested
     */
    BridgeTCPClient(Bus bus, std::shared_ptr<INetContext> ctx, std::string address);
    ///construct the client
    /**
     *
     * @param bus local end of the bus
     * @param address server's address:port
     * @param acb optional a callback which is called when authentication is requested
     * @note also creates network context with a one I/O thread
     */
    BridgeTCPClient(Bus bus, std::string address);

    ///Construct bridge, but don't connect yet
    /**
     * Use bind to connect
     * @param bus local end of the bus
     */
    BridgeTCPClient(Bus bus);

    virtual ~BridgeTCPClient() override;


    void set_linger_timeout(std::size_t timeout_ms);

    void bind(std::shared_ptr<INetContext> ctx, std::string address);

protected:

    virtual void on_timeout() noexcept override;
    virtual void on_channels_update() noexcept override;
    virtual void clear_to_send() noexcept override;
    virtual void receive_complete(std::string_view data) noexcept override;

    std::string _address;
    std::string _expected_ws_accept;
    std::string _header;
    std::string _session_id;
    std::size_t _linger_timeout = 1000;


    bool _timeout_reconnect = false;
    bool _send_reset_on_connect = false;
    bool _destructor_called = false;

    bool send_handshake();


    virtual void lost_connection() override;
    virtual void close() override;

    bool check_ws_response(std::string_view hdr);
    static std::string generate_session_id();
};



}
