#pragma once

#include "bridge.h"
#include "serialization.h"
#include "network.h"

#include <mutex>
namespace zerobus {

class BridgePipe: public AbstractBridge, public IPeer, public IMonitor {
public:

    static constexpr std::size_t _input_buffer_size = 4096;

    virtual void on_channels_update() noexcept override;
    BridgePipe(Bus bus, std::shared_ptr<INetContext> ctx,  ConnHandle read, ConnHandle write);
    ~BridgePipe();

    BridgePipe(const BridgePipe &) = delete;
    BridgePipe &operator=(const BridgePipe &) = delete;


    ///connect bridge to standard input and output
    /**
     * @param bus instance of bus
     * @param ctx network context
     * @return bridge instance
     */
    static BridgePipe connect_stdinout(Bus bus, std::shared_ptr<INetContext> ctx);

    ///connect bridge to standard input and output
    /**
     * @param bus instance of bus
     * @return bridge instance
     */
    static BridgePipe connect_stdinout(Bus bus);


    ///spawn child process and connect to child's standard input and output
    /**
     * @param bus bus instance of bus
     * @param ctx network context
     * @param command_line command line
     * @param tkn stop token - optional, triggering stop token cause termination of the process
     * @param exit_action optional function called when process exits
     * @return bridge instance
     */
    static BridgePipe connect_process(Bus bus, std::shared_ptr<INetContext> ctx,
            std::string_view command_line,
            std::stop_token tkn = {},
            std::function<void(int)> exit_action = {});

    ///spawn child process and connect to child's standard input and output
    /**
     * @param bus bus instance of bus
     * @param command_line command line
     * @param tkn stop token - optional, triggering stop token cause termination of the process
     * @param exit_action optional function called when process exits
     * @return bridge instance
     */
    static BridgePipe connect_process(Bus bus,
            std::string_view command_line,
            std::stop_token tkn = {},
            std::function<void(int)> exit_action = {});



protected:
    virtual void send(const AddToGroup&msg) noexcept override;
    virtual void send(const ChannelReset&msg) noexcept override;
    virtual void send(const GroupEmpty&msg) noexcept override;
    virtual void send(const ChannelUpdate &msg) noexcept override;
    virtual void send(const NewSession&msg) noexcept override;
    virtual void send(const CloseGroup&msg) noexcept override;
    virtual void send(const NoRoute&msg) noexcept override;
    virtual void send(const Message &msg) noexcept override;
    virtual void send(const UpdateSerial&msg) noexcept override;

    virtual void receive_complete(std::string_view data) noexcept override;
    virtual void clear_to_send() noexcept override;
    virtual void on_timeout() noexcept override;

    virtual void on_disconnect() noexcept {}
protected:
    std::shared_ptr<INetContext> _ctx;
    ConnHandle _h_read;
    ConnHandle _h_write;
    std::vector<char> _output_buffer;
    char _input_buffer[_input_buffer_size];
    std::vector<char> _msg_tmp_buffer;
    bool _clear_to_send = false;
    Serialization _ser;
    Deserialization _deser;

    std::mutex _mx;

    void ready_to_send();
    void ready_to_receive();
    bool flush_output();
    std::string_view combine_input_before_parse(const std::string_view &data);
    void combine_input_after_parse(const std::string_view &data);
    std::string_view parse_messages(const std::string_view &data);
    using AbstractBridge::receive;
    void receive(const  Deserialization::UserMsg &) {}
    template<typename T> void send_gen(const T &msg);
};


}
