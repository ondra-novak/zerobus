#pragma once
#include "network.h"
#include "network_linux_epollpp.h"
#include "cluster_alloc.h"
#include <condition_variable>
#include <memory>
#include <thread>
#include <set>


namespace zerobus {

class NetContext;



class NetContext: public INetContext, public std::enable_shared_from_this<NetContext> {
public:


    virtual ConnHandle peer_connect(std::string address) override;
    virtual void reconnect(ConnHandle ident, std::string address_port) override;
    ///start receiving data
    virtual void receive(ConnHandle ident, std::span<char> buffer, IPeer *peer) override;
    ///send data
    virtual std::size_t send(ConnHandle ident, std::string_view data) override;

    virtual void ready_to_send(ConnHandle ident, IPeer *peer) override;

    ///creates server
    virtual ConnHandle create_server(std::string address_port) override;

    ///request to accept next connection
    virtual void accept(ConnHandle ident, IServer *server) override;

    ///request to destroy server
    virtual void destroy(ConnHandle ident) override;

    virtual bool sync_wait(ConnHandle connection, std::atomic<std::uintptr_t> &var, std::uintptr_t block_value, std::chrono::system_clock::time_point timeout) override;
    virtual void sync_notify(ConnHandle connection) override;

    std::jthread run_thread();

    void run(std::stop_token tkn);

    virtual void set_timeout(ConnHandle ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) override;

    ///Clear existing timeout
    virtual void clear_timeout(ConnHandle ident) override;

    virtual void yield(ConnHandle connection, std::function<void()> fn) override;

protected:

    using MyEPoll = EPoll<ConnHandle>;
    using WaitRes = MyEPoll::WaitRes;
    using TimeoutInfo = std::pair<std::chrono::system_clock::time_point, ConnHandle>;
    using TimeoutSet = std::set<TimeoutInfo,  std::less<TimeoutInfo>, ClusterAlloc<TimeoutInfo> >;


    struct SocketInfo {
        ConnHandle _ident = static_cast<ConnHandle>(-1);
        int _socket = -1;
        std::span<char> _recv_buffer;
        int _flags = 0;
        int _cur_flags = 0;
        std::chrono::system_clock::time_point _tmtp = {};
        IPeer *_recv_cb = {};
        IPeer *_send_cb = {};
        IServer *_accept_cb = {};
        IPeerServerCommon *_timeout_cb = {};
        std::function<void()> _yield = {};
        int _cbprotect = {};
        bool _destroy_on_exit = false;

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
    MyEPoll _epoll = {};
    SocketList _sockets = {};
    ConnHandle _first_free_socket_ident = 0;
    TimeoutSet _tmset;
    std::condition_variable _cond;

    std::atomic<int> _cur_timer_thread = -1;


    void run_worker(std::stop_token tkn, int efd) ;
    SocketInfo *alloc_socket_lk();
    void free_socket_lk(ConnHandle id);
    SocketInfo *socket_by_ident(ConnHandle id);


    void process_event_lk(std::unique_lock<std::mutex> &lk, const  WaitRes &e, std::vector<std::function<void()> > &yield_queue);
    std::chrono::system_clock::time_point get_epoll_timeout_lk();

    void apply_flags_lk(SocketInfo *sock) noexcept;
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
