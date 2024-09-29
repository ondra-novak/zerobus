#pragma once
#include "network.h"

#include <memory>
#include <mutex>
#include <thread>
#include <sys/epoll.h>


namespace zerobus {

class NetContextAux { // @suppress("Miss copy constructor or assignment operator")
public:
    std::mutex mx;
    Socket sock = {};
    int flags = {};
    const void *timeout_ptr = {};
    bool server = false;
    std::span<char> buffer = {};
    NetContextAux(Socket s);
   ~NetContextAux();
};


class NetContext: public INetContext, public std::enable_shared_from_this<NetContext> {
public:

    NetContext();
    ~NetContext();

    NetContext(const NetContext &) = delete;
    NetContext &operator=(const NetContext &) = delete;


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


    static constexpr int events_per_wait = 16;
    int _epollfd = 0;
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


    std::vector<TimerInfo> _pqueue;
    std::atomic<int> _cur_timer_thread = -1;

    void process_event(const epoll_event &e);
    int get_epoll_timeout();
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
