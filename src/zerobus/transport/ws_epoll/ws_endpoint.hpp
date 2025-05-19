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



class AbstractWsEndpoint {
public:

    using Handle = std::size_t;


    AbstractWsEndpoint(unsigned int hk_sec, unsigned int threads = 0);
    virtual ~AbstractWsEndpoint() = default;

    bool send_message(std::string_view data, Handle slot);

    void bind(const std::string& address_port);
    void connect(const std::string& address_port);


protected:
    virtual void on_housekeeping() = 0;
    virtual void on_incoming_message(std::string_view data, std::string_view ident, Handle slot) = 0;

    struct SocketDeleter {void operator()(int);};
    using Socket = unique_handle<int, SocketDeleter>;


    struct Context {
        std::string _ident;
        Socket _sock = {};
        std::vector<char> _input_buffer;
        ws::Parser<std::vector<char> > _ws_parser;
        std::mutex _send_mx;
        std::string _reconnect_addr;
        bool _awaiting_header = true;
        bool _connecting = false;
        Context(int fd):_sock(fd,{}), _ws_parser(_input_buffer) {}
    };

    HandleHashMap<std::shared_ptr<Context> > _handles;
    std::mutex _mx;

    Socket _listen_socket ={};
    Handle _listen_socket_handle ={};
    EventFd _wakeup;
    EPoll<Handle> _epoll;
    unsigned int _hk_sec;
    std::atomic<std::chrono::system_clock::time_point> _next_hk = {};


    utils::MultiThread _pool;


    void worker(std::stop_token stp, const bool &kf);
    bool process_read_data(const std::shared_ptr<Context> &ctx, Handle h);
    void close_handle(Handle h);

    bool process_ws_message(const std::shared_ptr<Context> &ctx, ws::Message msg, Handle h);
    bool send_msg(const std::shared_ptr<Context> &ctx, ws::Message msg);
    std::optional<std::string_view> process_http_header(const std::shared_ptr<Context> &ctx, std::string_view data);
    void delay_connect(const std::shared_ptr<Context> &ctx, Handle h);

};


}
