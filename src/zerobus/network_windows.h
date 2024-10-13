#include "network.h"
#include "cluster_alloc.h"
#include <condition_variable>
#include <memory>
#include <thread>
#include <set>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>

namespace zerobus {

class NetContext: public INetContext, public std::enable_shared_from_this<NetContext> {
public:

    NetContext();
    ~NetContext();
    NetContext(const NetContext &) = delete;
    NetContext &operator=(const NetContext &) = delete;

    virtual ConnHandle peer_connect(std::string address) override;
    virtual void reconnect(ConnHandle ident, std::string address_port) override;
    virtual void receive(ConnHandle ident, std::span<char> buffer, IPeer *peer) override;
    virtual std::size_t send(ConnHandle ident, std::string_view data) override;
    virtual void ready_to_send(ConnHandle ident, IPeer *peer) override;
    virtual ConnHandle create_server(std::string address_port) override;
    virtual void accept(ConnHandle ident, IServer *server) override;
    virtual void destroy(ConnHandle ident) override;
    virtual bool sync_wait(ConnHandle connection, std::atomic<std::uintptr_t> &var, std::uintptr_t block_value, std::chrono::system_clock::time_point timeout) override;
    virtual void sync_notify(ConnHandle connection) override;
    std::jthread run_thread();
    void run(std::stop_token tkn);
    virtual void set_timeout(ConnHandle ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) override;
    virtual void clear_timeout(ConnHandle ident) override;
    virtual void yield(ConnHandle connection, std::function<void()> fn) override;

protected:

    static constexpr ULONG_PTR key_offset = 2;
    static constexpr ULONG_PTR key_wakeup = 0;
    static constexpr ULONG_PTR key_exit = 1;

    using TimeoutInfo = std::pair<std::chrono::system_clock::time_point, ConnHandle>;
    using TimeoutSet = std::set<TimeoutInfo,  std::less<TimeoutInfo>, ClusterAlloc<TimeoutInfo> >;

    struct SocketInfo {
        ConnHandle _ident = static_cast<ConnHandle>(-1);
        SOCKET _socket = static_cast<SOCKET>(-1);
        std::span<char> _recv_buffer;
        std::chrono::system_clock::time_point _tmtp = {};
        IPeer *_recv_cb = {};
        IPeer *_send_cb = {};
        IServer *_accept_cb = {};
        IPeerServerCommon *_timeout_cb = {};
        std::function<void()> _yield = {};
        int _cbprotect = {};
        int _af;
        bool _clear_to_send = false;  //<socket is ready to send data
        bool _error = false;        //<error happened, disconnect this stream
        bool _connecting = true;    //<socket is connection - state for io completion port
        char _aux_buffer[1024]; 
        union {
            DWORD _aux_to_send ;     //count of bytes to send in _aux_buffer
            SOCKET _accept_socket ;
        };
        OVERLAPPED _recv_ovl;
        OVERLAPPED _send_ovl;

        ///invoke one of callbacks
        /**
         * @param lk global lock
         * @param cond condition variable (activated when waiting on exit)
         * @param fn a callback function
         */
        template<typename Fn>
        void invoke_cb(std::unique_lock<std::mutex> &lk, std::condition_variable &cond, Fn &&fn);
    };
    using SocketList = std::vector<SocketInfo>;


    mutable std::mutex _mx;
    HANDLE _completion_port;
    SocketList _sockets = {};
    ConnHandle _first_free_socket_ident = 0;
    TimeoutSet _tmset;
    std::condition_variable _cond;
    bool _need_timeout_thread = false;


    SocketInfo *alloc_socket_lk();
    void free_socket_lk(ConnHandle id);
    SocketInfo *socket_by_ident(ConnHandle id);
    ConnHandle connect_peer(std::string address_port);
    void run_worker(std::stop_token tkn) ;
    DWORD get_completion_timeout_lk();
    std::chrono::system_clock::time_point get_completion_timeout_tp_lk();
    void process_event_lk(std::unique_lock<std::mutex> &lk, ConnHandle h,  DWORD transfered, OVERLAPPED *ovr, DWORD error, std::vector<std::function<void()> > &yield_queue);

};

class NetThreadedContext: public NetContext {
public:

    NetThreadedContext(int threads);
    ~NetThreadedContext();
    void start();

protected:
    std::vector<std::jthread> _threads;
};


std::shared_ptr<INetContext> make_context(int iothreads);

}