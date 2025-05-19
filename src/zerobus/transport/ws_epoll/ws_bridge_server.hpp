#pragma once

#include "ws_bridge_config.hpp"

#include "../../bus.hpp"
#include "../../bridge.hpp"
#include "../../binary_transport.hpp"
#include "../../utils/multithreads.hpp"
#include "../../utils/unique_handle.hpp"
#include "../utils/handle_hash_map.hpp"
#include "../utils/ws_defs.hpp"
#include "../utils/epollpp.hpp"
#include "../utils/http_utils.hpp"


#include <thread>
#include <shared_mutex>

namespace zerobus {

class WsBridgeServer {
public:

    WsBridgeServer(Bus bus,
            std::string address,
            WsBridgeConfig config = {});
    ~WsBridgeServer();

protected:

    struct SocketDeleter {void operator()(int);};
    using Handle = std::size_t;
    using Socket = unique_handle<int, SocketDeleter>;


    class PeerContext {
    public:

        PeerContext(WsBridgeServer &owner, Socket socket);
        ~PeerContext() {}

        char *output_start(std::size_t sz);
        void output_commit(std::size_t sz);

        bool on_incoming_data(Handle h);

        int get_ident() const {return _socket.get();}

        void update_socket(Socket socket, std::string_view initial_data);

        const std::chrono::system_clock::time_point& get_last_activity() const;

    protected:

        void send_buffer_to_socket();
        void send(const ws::Message &msg);
        bool parse_header();
        bool on_ws_data(std::string_view data);

        WsBridgeServer &_owner;
        std::string _ident;
        Socket _socket;
        BinaryTransport<OutputTypeProxy<PeerContext *> > *_parser = nullptr;
        std::optional<Bridge> _br;
        mutable std::mutex _rmx;
        mutable std::mutex _wmx;
        ws::Parser<std::vector<char> &> _ws_parser;
        std::vector<char> _out_buffer;
        std::size_t _tmp_buff_size = 0;
        std::vector<char> _in_buffer;
        std::atomic<std::chrono::system_clock::time_point> _last_activity;
        bool _awaiting_header = true;
    };


    Bus _bus;
    HandleHashMap<std::unique_ptr<PeerContext> > _peers;
    BridgeOpMode _mode;
    unsigned int _housekeeping_sec;

    std::shared_mutex _mx;
    std::atomic<std::chrono::system_clock::time_point> _next_housekeeping;

    Socket _listen_socket;
    Handle _listen_socket_handle;
    EPoll<Handle> _epoll;

    bool connect_identity(Socket &sock, std::string_view ident, std::string_view initial_data);

    void listen_on_socket(int socket, Handle h);

    void do_housekeeping();
    static std::unique_ptr<AbstractTransport> create_binary_transport(PeerContext *out,
            BinaryTransport<OutputTypeProxy<PeerContext *> >  *& in);

    utils::MultiThread _pool;


};

}
