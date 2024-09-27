#pragma once
#include "network.h"
#include "binary_bridge.h"

#include <mutex>
namespace zerobus {

class BridgeTCPClient: public IPeer {
public:

    static std::shared_ptr<BridgeTCPClient> connect(Bus bus, std::shared_ptr<INetContext> ctx, std::string address);

    BridgeTCPClient(Bus bus, std::shared_ptr<INetContext> ctx, NetContextAux *aux, std::string address);
    virtual void on_send_available() override;
    virtual void on_read_complete(std::string_view data) override;
    virtual NetContextAux* get_context_aux() override;
    virtual void on_timeout() override;

protected:
    BinaryBridge _bridge;
    std::shared_ptr<INetContext> _ctx;
    NetContextAux *_aux;
    std::string _address;

    char _input_buffer[2048];

    std::mutex _mx;
    std::vector<char> _output_buff;
    std::vector<char> _input_buff;
    bool _output_allowed = false;
    bool _timeout_reconnect = false;


    void output_fn(std::string_view data);
    void reconnect();
    void begin_read();
};

}
