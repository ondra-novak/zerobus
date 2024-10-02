#pragma once
#include "utility.h"
#include <chrono>
#include <vector>

namespace zerobus {


///schedules local events
template<typename T, template<class> class ContImpl = std::vector>
class TimeoutControler {
public:

    using Item = std::pair<std::chrono::system_clock::time_point, T>;
    using Cont = ContImpl<Item>;

    void set_event(std::chrono::system_clock::time_point tp, T event) {
        auto iter = std::find_if(_cont.begin(), _cont.end(), [&](const auto &x){
            return x.second = event;
        });
        if (iter == _cont.end()) {
            auto idx = _cont.size();
            _cont.push_back({tp, event});
            heapify_up(_cont, idx, compare);
        } else {
            auto idx = std::distance(_cont.begin(), iter);
            _cont.push_back({tp, event});
            heapify_remove(_cont, idx, compare);
        }
    }
    void cancel_event(T event) {
        auto iter = std::find_if(_cont.begin(), _cont.end(), [&](const auto &x){
            return x.second = event;
        });
        if (iter == _cont.end()) return;
        auto idx = std::distance(_cont.begin(), iter);
        heapify_remove(_cont, idx, compare);
    }
    bool empty() const {
        return _cont.empty();
    }

    auto next_event_time() const {
        return _cont.front().first;
    }

    T pop_next() {
        T out = std::move(_cont.front().second);
        heapify_remove(_cont, 0, compare);
    }

protected:
    Cont _cont;

    static bool compare(const Item &a, const Item &b) {
        return a.first > b.first;
    }
};


}
