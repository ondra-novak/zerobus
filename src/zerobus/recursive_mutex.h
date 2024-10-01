#pragma once

#include <mutex>
#include <atomic>
#include <thread>

namespace zerobus {



class RecursiveMutex {
public:

    void lock() {
        std::thread::id cur = {};
        while (!_owner.compare_exchange_strong(cur, std::this_thread::get_id())) {
            if (cur == std::this_thread::get_id()) {
                ++_recursion;
                return;
            }
            _owner.wait(cur);
            cur = {};
        }
        ++_recursion;

    }
    void unlock() {
        if (--_recursion) return;
        _owner = std::thread::id{};
        _owner.notify_all();

    }
    bool try_lock() {
        std::thread::id cur = {};
        if (!_owner.compare_exchange_strong(cur, std::this_thread::get_id())) {
            if (cur == std::this_thread::get_id()) {
                ++_recursion;
                return true;
            }
            return false;
        }
        return true;

    }

    unsigned int level() const {return _recursion;}
    bool level1() const {return _recursion == 1;}


protected:
    std::atomic<std::thread::id> _owner = {};
    unsigned int _recursion = 0;
};


}
