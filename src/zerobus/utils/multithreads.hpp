#pragma once

#include <stop_token>
#include <thread>
#include <vector>

namespace zerobus {

namespace utils {


class MultiThread {
public:

    template<std::invocable<std::stop_token> Fn>
    void add_threads(unsigned int count, Fn worker) {
        _threads.reserve(_threads.size()+count);
        for (unsigned int i = 0; i < count; ++i) {
            _threads.push_back(
                std::thread([worker, stp =_stop_src.get_token()]()mutable{
                    worker(stp);
                })
            );
        }
    }
    void stop_threads() {
        _stop_src.request_stop();
        auto id = std::this_thread::get_id();
        for (auto &t: _threads) {
            if (t.get_id() == id) {
                t.detach();
            } else {
                t.join();
            }
        }
        _threads.clear();
    }

    ~MultiThread() {
        stop_threads();
    }

    std::stop_token get_stop_token() const {
        return _stop_src.get_token();
    }

    std::size_t count() const {return _threads.size();}

    void request_stop() {
        _stop_src.request_stop();
    }

protected:


    std::vector<std::thread> _threads;
    std::stop_source _stop_src;

};


}

}
