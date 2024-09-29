#pragma once

#include "bridge_tcp_common.h"
#include <mutex>
namespace zerobus {

///Implements bridge over TCP
class BridgeTCPClient: public BridgeTCPCommon, public IMonitor {
public:


    struct AuthResponse  {
        std::string_view ident;
        std::string_view proof;
    };

    struct AuthRequest {
        std::string_view digest;
        std::string_view salt;
    };

    using AuthCallback = std::function<void(AuthRequest, std::function<void(AuthResponse)>)>;

    ///construct the client
    /**
     * @param bus local end of the bus
     * @param ctx network context
     * @param address server's address:port
     * @param acb optional a callback which is called when authentication is requested
     */
    BridgeTCPClient(Bus bus, std::shared_ptr<INetContext> ctx, std::string address, AuthCallback acb = {});
    ///construct the client
    /**
     *
     * @param bus local end of the bus
     * @param address server's address:port
     * @param acb optional a callback which is called when authentication is requested
     * @note also creates network context with a one I/O thread
     */
    BridgeTCPClient(Bus bus, std::string address, AuthCallback acb = {});

    virtual ~BridgeTCPClient() override;

protected:

    virtual void on_timeout() override;
    virtual void on_channels_update() noexcept override;
    virtual bool on_message_dropped(IListener *, const Message &) noexcept override {return false;}

    virtual void on_auth_request(std::string_view proof_type, std::string_view salt) override;

    std::string _address;
    AuthCallback _acb;

    bool _timeout_reconnect = false;



    virtual void lost_connection();

};

}
