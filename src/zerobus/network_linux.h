#pragma once
#include "network.h"
#include "epollpp.h"
#include "recursive_mutex.h"

#include <memory>
#include <thread>


namespace zerobus {

class NetContext;

class NetContextAux { // @suppress("Miss copy constructor or assignment operator")
public:
    RecursiveMutex mx;
    Socket sock = {};
    std::size_t ident = 0;
    int flags = 0;
    int cur_flags = 0;
    const void *timeout_ptr = {};
    bool server = false;
    IPeerServerCommon *peer = {};
    std::span<char> buffer = {};
    NetContextAux(Socket s);
   ~NetContextAux();

   void on_unlock();
};


class NetContext: public INetContext, public std::enable_shared_from_this<NetContext> {
public:


    virtual NetContextAux *peer_connect(std::string address) override;
    virtual void reconnect(IPeer *peer, std::string address_port) override;
    ///start receiving data
    virtual void receive(std::span<char> buffer, IPeer *peer) override;
    ///send data
    virtual std::size_t send(std::string_view data, IPeer *peer) override;

    virtual void callback_on_send_available(IPeer *peer) override;

    ///creates server
    virtual NetContextAux *create_server(std::string address_port) override;

    ///request to accept next connection
    virtual void accept(IServer *server) override;

    ///request to destroy server
    virtual void destroy(IPeerServerCommon *server) override;

    std::jthread run_thread();

    void run(std::stop_token tkn);

    virtual void set_timeout(std::chrono::system_clock::time_point tp, IPeerServerCommon *p) override;

    ///Clear existing timeout
    virtual void clear_timeout(IPeerServerCommon *p) override;


protected:

    using MyEPoll = EPoll<std::size_t>;
    using WaitRes = MyEPoll::WaitRes;

    MyEPoll _epoll;
    void run_worker(std::stop_token tkn, int efd) ;


    class TimerInfo {
    public:
        TimerInfo() = default;
        ~TimerInfo();
        TimerInfo(std::chrono::system_clock::time_point tp, IPeerServerCommon *p);
        TimerInfo(TimerInfo &&pos);
        TimerInfo &operator=(TimerInfo &&pos);
        TimerInfo(const TimerInfo &pos) = delete;
        TimerInfo &operator=(const TimerInfo &pos) = delete;

        void refresh_pos();

        std::chrono::system_clock::time_point get_tp() const;
        IPeerServerCommon *get_peer() const;

        static bool compare(const TimerInfo &a, const TimerInfo &b) {
            return a._tp > b._tp;
        }


    protected:
        IPeerServerCommon *_peer = {};
        std::chrono::system_clock::time_point _tp = {};
    };

    mutable std::recursive_mutex _tmx;
    mutable std::recursive_mutex _imx;

    std::vector<std::unique_ptr<NetContextAux> > _identMap;
    std::vector<TimerInfo> _pqueue;
    std::atomic<int> _cur_timer_thread = -1;

    void process_event(const  WaitRes &e);
    std::chrono::system_clock::time_point get_epoll_timeout();


    void apply_flags(NetContextAux *aux);
    NetContextAux *lock_from_id(std::size_t id);
    NetContextAux *alloc_aux(Socket sock);
    void free_aux(NetContextAux *aux);
    std::size_t _aux_first_free = 0;
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
