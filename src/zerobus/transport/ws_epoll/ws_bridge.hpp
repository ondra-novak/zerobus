#pragma once
#include <string_view>
#include <vector>
#include "ws_bridge_config.hpp"
#include "../../bus.hpp"
#include "../../bridge.hpp"
#include "../../binary_transport.hpp"
#include "../utils/ws_defs.hpp"
#include "../utils/handle_hash_map.hpp"
#include "../utils/epollpp.hpp"
#include "../../utils/unique_handle.hpp"
#include "../../utils/multithreads.hpp"
#include "../utils/eventfd.hpp"


#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

namespace zerobus {



class WsBridge {
public:

    using Handle = std::size_t;


    WsBridge(Bus bus, WsBridgeConfig config);
    ~WsBridge();

    ///bind to port
    Handle bind(const std::string& address_port);
    ///add new connection
    Handle connect(const std::string& address_port);
    ///close existing connection
    void close(Handle h);


protected:


    struct SocketDeleter {void operator()(int);};
    using Socket = unique_handle<int, SocketDeleter>;


    enum class PeerOpMode {
        ///message mode (in/out)
        message,
        ///awaiting response after request (client)
        await_response,
        ///awaiting request (server)
        await_request,
        ///waiting for connect
        connecting,
        ///in reconnect mode
        reconnect,
    };

    struct Shared {
        WsBridgeConfig _config;
        EPoll<Handle> _epoll;
        EventFd _wakeup;
        struct SSL_CTX_Deleter {void operator()(SSL_CTX *_);};
        std::unique_ptr<SSL_CTX, SSL_CTX_Deleter> _ssl_client_ctx;
        std::unique_ptr<SSL_CTX, SSL_CTX_Deleter> _ssl_server_ctx;

        Shared(WsBridgeConfig config):_config(std::move(config)) {}
    };

    using PShared = std::shared_ptr<Shared>;

    class Peer: public Bridge { // @suppress("Miss copy constructor or assignment operator")
    public:

        static constexpr int write_timeout = 1500;

        char *output_start(std::size_t sz, Importance imp);
        DeliveryError output_commit(std::size_t sz, Importance imp);
        int on_epoll_event(int event) noexcept;

        bool send_message(std::unique_lock<std::mutex> &lk, const ws::Message &msg, Importance impl);
        std::optional<std::string_view> read_http_header(std::string_view data);

        Peer(WsBridge &owner);
        Peer(WsBridge &owner, int socket);
        Peer(WsBridge &owner, std::string address);
        ~Peer() {disconnect();}

        std::pair<int, int> get_epoll_info() const;

        void set_handle(Handle h){_h = h;}

        void connect(std::string address);

        bool keep_alive();

    protected:
        bool on_epoll_in() noexcept;
        bool on_epoll_out() noexcept;
        bool conn_error(std::unique_lock<std::mutex> &lk);
        bool conn_error();
        bool flush_buffer();
        bool send_ws_request();

        bool finish_send(std::unique_lock<std::mutex> &lk, Importance imp);
        bool direct_send(std::string_view data);

        PShared _shared;
        Socket _sock = {};
        Handle _h = 0;
        PeerOpMode _mode = {};
        std::vector<char> _input_buffer;
        std::vector<char> _output_buffer;
        std::vector<char> _build_buffer;
        ws::Parser<std::vector<char> > _ws_parser;
        mutable std::mutex _send_mx;
        std::condition_variable _cv;
        std::string _reconnect_addr;
        std::string _ws_accept;
        std::optional<std::default_random_engine> _mask_rnd;
        BinaryTransport<OutputTypeProxy<Peer *> > *_bridge_parser;
        std::atomic_flag _in_handler = {false};
        int _kl = 0;
        struct SSL_Deleter {void operator()(SSL *_);};
        std::unique_ptr<SSL, SSL_Deleter> _ssl_sock;
        bool _need_handshake = false;
        int _ssl_want_mode = 0;

        int process_ssl_error(int r) noexcept;
    };

    class Server { // @suppress("Miss copy constructor or assignment operator")
    public:

        Server(WsBridge &owner, int socket);
        int on_epoll_event(int event);
        std::pair<int, int> get_epoll_info() const;
        bool keep_alive() {return true;}

    protected:

        WsBridge &_owner;
        Socket _sock = {};
    };


    using PPeer =std::shared_ptr<Peer>;
    using PServer = std::shared_ptr<Server>;
    using PHandleData = std::variant<PPeer, PServer>;


    Bus _bus;
    std::mutex _mx;
    PShared _shared;

    HandleHashMap<PHandleData> _handles;
    std::atomic<std::chrono::system_clock::time_point> _next_hk = {};

    utils::MultiThread _pool;

    std::unique_ptr<AbstractTransport> create_transport(Peer *peer, BinaryTransport<OutputTypeProxy<Peer *> > * &parser);

    void create_peer(int socket);
    void ensure_threads_running();
    void worker(std::stop_token stp);
    void housekeeping();


};

#ifdef WITH_TLS
class SSLError : public std::runtime_error {
public:
    SSLError(const std::string& msg);
private:
    static std::string getOpenSSLErrors();
};
#endif

}
