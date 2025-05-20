#pragma once
#include <shared_mutex>
#include <thread>
namespace zerobus {


class recursive_shared_mutex: public std::shared_mutex {
public:

    using Super = std::shared_mutex;

    void lock() {
        auto thr = std::this_thread::get_id();
        if (_exclusive_owner == thr) {
            ++_exclusive_count;
            return;
        }
        Super::lock();
        _exclusive_owner = thr;
        _exclusive_count = 1;
    }
    [[nodiscard]] bool try_lock() {
        auto thr = std::this_thread::get_id();
        if (_exclusive_owner == thr) {
            ++_exclusive_count;
            return true;
        }
        if (Super::try_lock()) {
            _exclusive_owner = thr;
            _exclusive_count = 1;
            return true;
        }
        return false;
    }
    void unlock() {
        if (--_exclusive_count == 0) {
            _exclusive_owner = {};
            Super::unlock();
        }
    }

    void lock_shared() {
        if (inc_recursion()) {
            Super::lock_shared();
        }
    }

    [[nodiscard]] bool try_lock_shared() {
        auto cnt = get_recursion();
        if (cnt) {
            ++(*cnt);
            return true;
        }
        if (Super::try_lock_shared()) {
            inc_recursion();
            return true;
        }
        return false;
    }

    void unlock_shared() {
        if (dec_recursion())
            std::shared_mutex::unlock_shared();
    }

    std::size_t recursion_count_shared() const {
        auto cnt = get_recursion();
        return cnt?*cnt:0;
    }

    std::size_t recursion_count() const {
        return _exclusive_count;
    }


protected:

    struct RecursionRec { // @suppress("Miss copy constructor or assignment operator")
        recursive_shared_mutex *owner = nullptr;
        std::size_t count = 0;
    };

    using RecursiveTable = std::vector<RecursionRec>;

    std::thread::id _exclusive_owner = {};
    std::size_t _exclusive_count = 0;

    static thread_local RecursiveTable _recursion;

    auto find_owner() const {
        return [this](const RecursionRec &rec) {
            return rec.owner == this;
        };
    }

    std::size_t *get_recursion() const {
        auto iter = std::find_if(_recursion.begin(), _recursion.end(), find_owner());
        if (iter == _recursion.end()) return nullptr;
        return &(iter->count);
    }

    bool inc_recursion() {
        auto cnt = get_recursion();
        if (!cnt) {
            _recursion.push_back({this, 1});
            return true;
        }
        ++(*cnt);
        return false;
    }

    bool dec_recursion() {
        auto cnt = get_recursion();
        if (!cnt) return true;
        if (--(*cnt) == 0) {
            _recursion.erase(
                std::remove_if(_recursion.begin(), _recursion.end(), find_owner()),
                _recursion.end());
            return true;
        }
        return false;
    }

};

inline thread_local recursive_shared_mutex::RecursiveTable recursive_shared_mutex::_recursion = {};


}
