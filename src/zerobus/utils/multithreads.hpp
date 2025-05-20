#pragma once

#include <stop_token>
#include <thread>
#include <vector>

namespace zerobus {

namespace utils {


class MultiThread {
public:

    template<std::invocable<std::stop_token, const bool &> Fn>
    void add_threads(unsigned int count, Fn worker) {
        _threads.reserve(_threads.size()+count);
        for (unsigned int i = 0; i < count; ++i) {
            _threads.push_back(ThreadReg{
                std::thread([this, worker, stp =_stop_src.get_token()]()mutable{
                    bool kflag = false;
                    register_kflag(&kflag);
                    worker(stp, kflag);
                })
            });
        }
    }
    void stop_threads() {
        _stop_src.request_stop();
        auto id = std::this_thread::get_id();
        for (auto &t: _threads) {
            if (t.thr.get_id() == id) {
                t.thr.detach();
                if (t.kflag) *t.kflag = true;
            } else {
                t.thr.join();
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

protected:

    struct ThreadReg { // @suppress("Miss copy constructor or assignment operator")
        std::thread thr;
        bool *kflag = nullptr;
    };

    std::vector<ThreadReg> _threads;
    std::stop_source _stop_src;
    void register_kflag(bool *kflag) {
        auto id = std::this_thread::get_id();
        auto iter = std::find_if(_threads.begin(), _threads.end(),
                [&](const ThreadReg &r) {return r.thr.get_id() == id;});
        if (iter != _threads.end()) {
            iter->kflag = kflag;
        }
    }
};


}

}
