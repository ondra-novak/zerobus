#pragma once
#include <string_view>
#include <vector>
#include "../utils/ws_defs.hpp"
#include "../utils/handle_hash_map.hpp"
#include "../utils/epollpp.hpp"
#include "../../utils/unique_handle.hpp"
#include "../../utils/multithreads.hpp"
#include "../utils/eventfd.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
namespace zerobus {



class WsEndpoint {
public:

    using Handle = std::size_t;


    WsEndpoint();
    virtual ~WsEndpoint() = default;

    bool send_message(std::string_view data, Handle slot);
    void send_ping(Handle slot);

    void bind(const std::string& address_port);
    void connect(const std::string& address_port);

    enum class RecStatus {
        message,
        timeout,
        interrupt,
        pong
    };

    struct Message {
        std::string_view ident;
        std::string_view data;
        Handle prev_handle;
    };

    RecStatus receive(Message &msg, std::chrono::system_clock::time_point timeout);

    void set_interrupt();

protected:

    struct SocketDeleter {void operator()(int);};
    using Socket = unique_handle<int, SocketDeleter>;


    struct Context {
        std::string _ident;
        Socket _sock = {};
        std::vector<char> _input_buffer;
        ws::Parser<std::vector<char> > _ws_parser;
        std::mutex _send_mx;
        std::string _reconnect_addr;
        std::string _ws_accept;
        bool _awaiting_header = true;
        bool _connecting = false;
        Context(int fd):_sock(fd,{}), _ws_parser(_input_buffer) {}
    };

    HandleHashMap<std::shared_ptr<Context> > _handles;
    std::mutex _mx;

    Socket _listen_socket ={};
    Handle _listen_socket_handle ={};
    Handle _last_handle = {};
    EventFd _wakeup;
    EPoll<Handle> _epoll;


    utils::MultiThread _pool;


    void close_conn(std::shared_ptr<Context> ctx, Handle h);

    enum class Res {
        error,
        ok,
        event,
        pong
    };

    Res process_ws_message(const std::shared_ptr<Context> &ctx, ws::Message &msg);
    Res process_read_data(const std::shared_ptr<Context> &c);
    bool send_msg(const std::shared_ptr<Context> &ctx, ws::Message msg);
    std::optional<std::string_view> process_http_header(const std::shared_ptr<Context> &ctx, std::string_view data);
    void delay_connect(const std::shared_ptr<Context> &ctx, Handle h);
    bool send_ws_request(const std::shared_ptr<Context> &ctx);
    bool process_read_event(Handle h);
};


}
