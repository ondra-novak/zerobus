#pragma once
#include "network.h"
#include "bridge.h"
#include "timeout_control.h"
#include "websocket.h"
#include "serialization.h"
#include <mutex>
namespace zerobus {

class BridgeTCPCommon: public AbstractBridge, public IPeer {
public:

    static constexpr int input_buffer_size = 8192;
    static constexpr std::string_view magic = "zbus";


    virtual ~BridgeTCPCommon() override;
    BridgeTCPCommon(const BridgeTCPCommon &) = delete;
    BridgeTCPCommon &operator=(const BridgeTCPCommon &) = delete;

    static std::string get_address_from_url(std::string_view url);
    static std::string get_path_from_url(std::string_view url);
    static std::string calculate_ws_accept(std::string_view key);
    static std::string generate_ws_key();

    void set_hwm(std::size_t hwm);

protected:

    virtual void clear_to_send() noexcept override;
    virtual void receive_complete(std::string_view data) noexcept override;
    virtual void output_message(std::string_view message) ;
    virtual void on_timeout() noexcept override;

    virtual void lost_connection() {}

    std::shared_ptr<INetContext> _ctx;
    ConnHandle _aux;
    ws::Builder _ws_builder;
    ws::Parser _ws_parser;
    std::size_t _hwm = 1024*1024;   //1MB

    char _input_buffer[input_buffer_size];

    std::mutex _mx;

    std::vector<char> _output_data = {};
    std::vector<char> _input_data = {};
    std::vector<std::size_t> _output_msg_sp = {};
    std::size_t _output_cursor = 0;
    bool _handshake = true;
    bool _output_allowed = false;

    virtual void send(ChannelReset&&) noexcept override;
    virtual void send(CloseGroup&&) noexcept override;
    virtual void send(Message &&msg) noexcept override;
    virtual void send(ChannelUpdate &&msg) noexcept override;
    virtual void send(ClearPath&&) noexcept override;
    virtual void send(AddToGroup&&) noexcept override;
    void read_from_connection();

    bool after_send(std::size_t sz);
    std::string_view get_view_to_send() const;

    void flush_buffer();

    BridgeTCPCommon(Bus bus, std::shared_ptr<INetContext> ctx, ConnHandle aux, bool client);
    void init();

    void output_message(const ws::Message &msg);

    static std::string_view split(std::string_view &data, std::string_view sep);
    static std::string_view trim(std::string_view data);
    static char fast_tolower(char c);
    static bool icmp(const std::string_view &a, const std::string_view &b);


    template<std::invocable<std::string_view, std::string_view> CB>
    std::string_view parse_header(std::string_view hdr, CB &&cb) {
        auto first_line = split(hdr, "\r\n");
        while (!hdr.empty()) {
            auto value = split(hdr, "\r\n");
            auto key = split(value,":");
            key = trim(key);
            value = trim(value);
            cb(key, value);
        }
        return first_line;

    }

    void deserialize_message(const std::string_view &msg);
    Deserialization _deser;
    static thread_local Serialization _ser;

    using AbstractBridge::receive;
    void receive(Deserialization::UserMsg &&) {}

};

}
