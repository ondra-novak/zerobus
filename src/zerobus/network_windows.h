#include "network.h"
#include <condition_variable>
#include <memory>
#include <memory_resource>
#include <thread>
#include <set>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include <source_location>

namespace zerobus {

class NetContextWin: public INetContext, public std::enable_shared_from_this<NetContextWin> {
public:

    explicit NetContextWin(ErrorCallback ecb);
    NetContextWin();
    ~NetContextWin();
    NetContextWin(const NetContextWin &) = delete;
    NetContextWin &operator=(const NetContextWin &) = delete;

    virtual ConnHandle peer_connect(std::string address) override;
    virtual void reconnect(ConnHandle ident, std::string address_port) override;
    virtual void receive(ConnHandle ident, std::span<char> buffer, IPeer *peer) override;
    virtual std::size_t send(ConnHandle ident, std::string_view data) override;
    virtual void ready_to_send(ConnHandle ident, IPeer *peer) override;
    virtual ConnHandle create_server(std::string address_port) override;
    virtual void accept(ConnHandle ident, IServer *server) override;
    virtual void destroy(ConnHandle ident) override;
    std::jthread run_thread();
    void run(std::stop_token tkn);
    virtual void set_timeout(ConnHandle ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) override;
    virtual void clear_timeout(ConnHandle ident) override;
    virtual void enqueue(std::function<void()> fn) override;

protected:

    static constexpr ULONG_PTR key_offset = 2;
    static constexpr ULONG_PTR key_wakeup = 0;
    static constexpr ULONG_PTR key_exit = 1;

    using TimeoutInfo = std::pair<std::chrono::system_clock::time_point, ConnHandle>;
    using TimeoutSet = std::set<TimeoutInfo,  std::less<TimeoutInfo>,  std::pmr::polymorphic_allocator<TimeoutInfo> >;


    struct SocketInfo {
        ConnHandle _ident = static_cast<ConnHandle>(-1);    //this connection handle
        SOCKET _socket = INVALID_SOCKET;                    //associated socket
        std::span<char> _recv_buffer;                       //reference to receiving buffer
        std::chrono::system_clock::time_point _tmtp = {};   //current scheduled timeout - function set_timeout()
        IPeer *_recv_cb = {};                               //callback object for recv
        IPeer *_send_cb = {};                               //callback object for send
        IServer *_accept_cb = {};                           //callback object for accept
        IPeerServerCommon *_timeout_cb = {};                //callback object for timeout
        int _cb_call_cntr = {};                                //count of currently active callbacks (must be 0 to destroy)
        bool _clear_to_send = false;                        //sending is allowed
        bool _error = false;                                //error reported and connection is lost
        bool _connecting = false;                            //socket is connecting (connect)
        char _aux_buffer[1024] = {};                        //a buffer used for accept or for send data during OVERLAPPED operation
        DWORD _to_send = 0;                                 //count of bytes to send in the buffer
        OVERLAPPED _send_ovr = {};                              //OVERLAPPED for send or connect
        OVERLAPPED _recv_ovr = {};                              //OVERLAPPED for recv or accept
        int _af;                                            //AF socket family of current socket (need for accept)
        SOCKET _accept_socket  = INVALID_SOCKET;            //current accept socket for server

        ///invoke one of callbacks
        /**
         * @param lk global lock
         * @param cond condition variable (activated when waiting on exit)
         * @param fn a callback function
         */
        template<typename Fn>
        void invoke_cb(std::unique_lock<std::mutex> &lk, std::condition_variable &cond, Fn &&fn);
    };
    using SocketList = std::vector<std::unique_ptr<SocketInfo> >;


    mutable std::mutex _mx;
    ErrorCallback _ecb;
    HANDLE _completion_port;
    SocketList _sockets = {};
    ConnHandle _first_free_socket_ident = 0;
    std::pmr::unsynchronized_pool_resource _pool;
    TimeoutSet _tmset;
    std::condition_variable _cond;
    bool _need_timeout_thread = false;
    std::vector<std::function<void()> > _actions;


    SocketInfo *alloc_socket_lk();
    void free_socket_lk(ConnHandle id);
    SocketInfo *socket_by_ident(ConnHandle id);
    ConnHandle connect_peer(std::string address_port);
    void run_worker(std::stop_token tkn) ;
    DWORD get_completion_timeout_lk();
    std::chrono::system_clock::time_point get_completion_timeout_tp_lk();
    void process_event_lk(std::unique_lock<std::mutex> &lk, ConnHandle h,  DWORD transfered, OVERLAPPED *ovr, DWORD error);
    template<typename E> void report_error(E exception, std::string_view action, std::source_location loc = std::source_location::current());
    void report_last_error(std::string_view action, std::source_location loc = std::source_location::current());
};

class NetThreadedContext: public NetContextWin {
public:

    NetThreadedContext(ErrorCallback ecb, int threads);
    ~NetThreadedContext();
    void start();

protected:
    std::vector<std::jthread> _threads;
};




class Win32ErrorCategory: public std::error_category {
public:
    virtual ~Win32ErrorCategory() noexcept = default;
    virtual const char* name() const noexcept override;
    virtual std::string message(int _Errval) const override;
};


class Win32Error: public std::system_error {
public:
    Win32Error();
    Win32Error(std::string message);
    Win32Error(DWORD error);
    Win32Error(DWORD error, std::string message);
};

}
