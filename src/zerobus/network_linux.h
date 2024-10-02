#pragma once
#include "network.h"
#include "epollpp.h"
#include "recursive_mutex.h"

#include <condition_variable>
#include <memory>
#include <thread>
#include <set>


namespace zerobus {

class NetContext;



class NetContext: public INetContext, public std::enable_shared_from_this<NetContext> {
public:


    virtual SocketIdent peer_connect(std::string address) override;
    virtual void reconnect(SocketIdent ident, std::string address_port) override;
    ///start receiving data
    virtual void receive(SocketIdent ident, std::span<char> buffer, IPeer *peer) override;
    ///send data
    virtual std::size_t send(SocketIdent ident, std::string_view data) override;

    virtual void callback_on_send_available(SocketIdent ident, IPeer *peer) override;

    ///creates server
    virtual SocketIdent create_server(std::string address_port) override;

    ///request to accept next connection
    virtual void accept(SocketIdent ident, IServer *server) override;

    ///request to destroy server
    virtual void destroy(SocketIdent ident) override;

    std::jthread run_thread();

    void run(std::stop_token tkn);

    virtual void set_timeout(SocketIdent ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) override;

    ///Clear existing timeout
    virtual void clear_timeout(SocketIdent ident) override;


protected:

    using MyEPoll = EPoll<SocketIdent>;
    using WaitRes = MyEPoll::WaitRes;
    using TimeoutSet = std::set<std::pair<std::chrono::system_clock::time_point, SocketIdent>  >;


    struct SocketInfo {
        SocketIdent _ident = static_cast<SocketIdent>(-1);
        int _socket = -1;
        std::span<char> _recv_buffer;
        int _flags = 0;
        int _cur_flags = 0;
        std::chrono::system_clock::time_point _tmtp = {};
        IPeer *_recv_cb = {};
        IPeer *_send_cb = {};
        IServer *_accept_cb = {};
        IPeerServerCommon *_timeout_cb = {};
        int _cbprotect = {};

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
    SocketIdent _first_free_socket_ident = 0;
    TimeoutSet _tmset;
    std::condition_variable _cond;

    std::atomic<int> _cur_timer_thread = -1;


    void run_worker(std::stop_token tkn, int efd) ;
    SocketInfo *alloc_socket_lk();
    void free_socket_lk(SocketIdent id);
    SocketInfo *socket_by_ident(SocketIdent id);


    void process_event_lk(std::unique_lock<std::mutex> &lk, const  WaitRes &e);
    std::chrono::system_clock::time_point get_epoll_timeout_lk();

    void apply_flags_lk(SocketInfo *sock);
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
