#pragma once

#include "../utils/epollpp.hpp"
#include <mutex>
#include <thread>
#include <unordered_map>
#include <set>
#include <chrono>
#include <vector>


///Generic event dispatcher
/**
 * @tparam Context Type of context object. The dispatcher works with its pointer
 */
template<typename Context>
class EpollDispatcher {
public:

    ///Callback
    /**
     * @param ctx associated context
     * @param now current time
     * @param flags epoll flags which triggered this event. Timeout if zero
     */
    using Callback = void (*)(Context *ctx, std::chrono::system_clock::time_point now, int flags);

    EpollDispatcher() {
        _ep.add(_reschedule.get_fd(), EPOLLIN|EPOLLET, nullptr);
    }

    ///helper deleter for unique_context();
    struct ContextUnreg {
        EpollDispatcher *dhis;
        void operator()(Context *ctx) const {
            dhis->unwatch(ctx);
        }
    };

    ///creates unique ptr context which unregisters when unique ptr is removed
    std::unique_ptr<Context, ContextUnreg> unique_context(Context *ctx) {
        return {ctx, {this}};
    }

    ///Register context to be watcher
    /**
     * If context is already watched, its watch conditions are changed.
     *
     * @param ctx pointer to context
     * @param filedesc associated file descriptor
     * @param flags epoll flags
     * @param timeout timeout absolute point (can be max())
     * @param cb callback which is called when event is triggered or timeout
     *
     * @note when callback is called futher events are supressed. You need to
     * call watch again.
     *
     * @note you must always call unwatch before context is destroyed, even if
     * event was successfully processed
     */
    void watch(Context *ctx,int filedesc, int flags, std::chrono::system_clock::time_point timeout, Callback cb) {
        std::lock_guard _(_mx);

        RegData &rg = _resources[ctx];

        //file description change
        if (rg.filedesc != filedesc) {
            if (rg.filedesc != -1) _ep.del(rg.filedesc);
            rg.filedesc = filedesc;
            _ep.add(rg.filedesc, flags, ctx);
        } else {
            _ep.mod(rg.filedesc, flags, ctx);
        }


        //timeout change
        if (rg.timeout != timeout) {
            if (rg.timeout != std::chrono::system_clock::time_point::max()) {
                _timeouts.erase({rg.timeout, ctx});
            }
            rg.timeout = timeout;
            if (rg.timeout != std::chrono::system_clock::time_point::max()) {
                auto r2 = _timeouts.insert({rg.timeout,ctx});
                if (r2.first == _timeouts.begin()) {
                    _reschedule.set();
                }
            }
        }

        //reg callback
        rg.cb = cb;
        //flags
        rg.flags = flags;
    }

    ///unwatch the context
    /**
     * @param ctx context to unwatch
     * @note Even if context is currently in supressed state, you still need to
     * call unwatch, if context is destroyed
     */

    void unwatch(Context *ctx) {
        std::lock_guard _(_mx);
        auto iter = _resources.find(ctx);
        if (iter != _resources.end()) {
            if (iter->second.filedesc != -1) {
                _ep.del(iter->second.filedesc);
            }
            if (iter->second.timeout != std::chrono::system_clock::time_point::max()) {
                _timeouts.erase({iter->second.timeout, ctx});
            }
            _resources.erase(iter);
        }
    }

    ///run dispatcher
    /**
     * @param stp stop token to stop function
     * @note it is allowed to run multiple threads of this function
     */
    void run(std::stop_token stp) {
        //each thread has own stop event, which is triggered by stop token
        EventFd stp_ev;
        //this function remove context
        auto stp_ctx = unique_context(reinterpret_cast<Context *>(&stp_ev));
        //notify stop event when stop requested
        std::stop_callback stp_cb(stp,[&]{stp_ev.set();});
        //register stop event for this thread
        //callback can be nullptr so no callback is called
        watch(stp_ctx.get(), stp_ev.get_fd(), EPOLLIN,
                std::chrono::system_clock::time_point::max(),nullptr);
        //cycle until stop requested
        while (!stp.stop_requested()) {
            //poll timeout
            std::chrono::system_clock::time_point tstop;
            {
                std::lock_guard _(_mx);
                //explore timeouts and find first timestamp
                auto b = _timeouts.begin();
                if (b == _timeouts.end())
                    tstop = tstop.max();
                else
                    tstop = b->first;
            }
            //wait for event
            auto wtr = _ep.wait(tstop);
            auto now = std::chrono::system_clock::now();
            Ready ready;
            //detect timeout - wtr must have value
            if (wtr.has_value()) {
                //fetch events
                std::uint32_t events = wtr->events;
                //fetch context
                Context *ctx = wtr->ident;
                //if context is null - rescheduler equested
                if (ctx == nullptr) {
                    //continue in cycle
                    continue;
                }
                //under lock
                std::lock_guard _(_mx);
                //find resource
                auto iter = _resources.find(ctx);
                    //resource found
                if (iter != _resources.end()) {
                    //retrieve register data
                    RegData &rg = iter->second;
                    //flags must be expected - if not skip
                    if (rg.flags & events) {
                        //reset flags - no futher events
                        rg.flags = 0;
                        //reset timeout - no futher timeouts
                        rg.timeout = rg.timeout.max();
                        //prepare for execution
                        ready.cb = rg.cb;
                        ready.ctx = ctx;
                        ready.flags = events;
                    }
                }
            } else {
                //explore timeouts
                std::lock_guard _(_mx);
                auto titer = _timeouts.begin();
                if (titer != _timeouts.end() && titer->first < now) {
                    //there is timeouted context
                    Context *ctx = titer->second;
                    //check whether it is still registered
                    auto iter = _resources.find(ctx);
                    if (iter != _resources.end()) {
                        RegData &rg = iter->second;
                        //reset flags - no futher events
                        rg.flags = 0;
                        //reset timeout - no futher timeouts
                        rg.timeout = rg.timeout.max();
                        //prepare for execution
                        ready.cb = rg.cb;
                        ready.ctx = ctx;
                    }
                    _timeouts.erase(titer);
                }
            }
            //we have callback to call
            if (ready.cb) {
                //call it
                ready.cb(ready.ctx, now, ready.flags);
            }
            //repeat in cycle
        }
    }

protected:

    struct RegData {
        int filedesc = -1;
        int flags = 0;
        std::chrono::system_clock::time_point timeout = std::chrono::system_clock::time_point::max();
        Callback cb = nullptr;
    };

    struct Ready {
        Context *ctx = nullptr;
        Callback cb = nullptr;
        int flags = 0;
    };

    EPoll<Context *> _ep;
    EventFd _reschedule;
    std::unordered_map<Context *, RegData> _resources;
    std::set<std::pair<std::chrono::system_clock::time_point, Context *> > _timeouts;
    std::mutex _mx;

};

template<typename Context>
using PEpollDispatcher = std::shared_ptr<EpollDispatcher<Context> >;

template<typename Context>
PEpollDispatcher<Context> create_singlethreaded() {
    return std::make_shared<EpollDispatcher<Context> >();
}


template<typename Context>
class ThreadedEpollDispatcher : public EpollDispatcher<Context>{
public:
    ThreadedEpollDispatcher(unsigned int threads) {
        if (!threads) threads = std::max(1U, std::thread::hardware_concurrency());
        for (unsigned int i = 0; i < threads; ++i) {
            _threads.emplace_back([this](std::stop_token tkn){
                this->run(std::move(tkn));
            });
        }
    }
protected:
    std::vector<std::jthread> _threads;
};

template<typename Context>
PEpollDispatcher<Context> create_multithreaded(unsigned int threads) {
    return std::make_shared<ThreadedEpollDispatcher<Context> >(threads);
}
