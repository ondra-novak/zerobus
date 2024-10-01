#pragma once
#include "network.h"
#include "binary_bridge.h"
#include "timeout_control.h"

#include <mutex>
namespace zerobus {

class BridgeTCPCommon: public AbstractBinaryBridge, public IPeer {
public:

    static constexpr int input_buffer_size = 8192;
    static constexpr std::string_view magic = "zbus";


    BridgeTCPCommon(Bus bus, std::shared_ptr<INetContext> ctx, NetContextAux *aux);
    virtual ~BridgeTCPCommon() override;
    BridgeTCPCommon(const BridgeTCPCommon &) = delete;
    BridgeTCPCommon &operator=(const BridgeTCPCommon &) = delete;


protected:

    virtual void on_send_available() override;
    virtual void on_read_complete(std::string_view data) override;
    virtual NetContextAux* get_context_aux() override;
    virtual void on_auth_response(std::string_view ident,
            std::string_view proof, std::string_view salt) override;
    virtual void output_message(std::string_view message) override;
    virtual void on_auth_request(std::string_view proof_type,
            std::string_view salt) override;
    virtual void on_welcome() override;
    virtual void on_timeout() override;

    virtual void lost_connection() = 0;

    std::shared_ptr<INetContext> _ctx;
    NetContextAux *_aux;

    char _input_buffer[input_buffer_size];

    std::mutex _mx;

    std::vector<char> _output_data = {};
    std::vector<char> _input_data = {};
    std::vector<std::size_t> _output_msg_sp = {};
    std::size_t _output_cursor = 0;

    bool _output_allowed = false;


    void read_from_connection();

    bool after_send(std::size_t sz);
    std::string_view get_view_to_send() const;

    std::string_view parse_messages(std::string_view data);
    void flush_buffer();
};

}
