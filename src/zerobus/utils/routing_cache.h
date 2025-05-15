#pragma once

#include <chrono>
#include <memory>
#include <unordered_map>
#include <string>

namespace zerobus {

namespace utils {

template<typename Bridge>
class RoutingCache {
public:

    void set_timeout(std::chrono::system_clock::duration timeout) {
        _record_timeout = timeout;
    }

    void register_path(std::string_view target, Bridge bridge) {
        auto f = _cache_map.find(target);
        if (f != _cache_map.end()) {
            f->second->_bridge = bridge;
            f->second->_expiration = std::chrono::system_clock::now()+_record_timeout;
        } else {
            auto p = std::make_unique<Record>();
            p->_bridge = bridge;
            p->_expiration = std::chrono::system_clock::now()+_record_timeout;
            p->_target.append(target);
            _cache_map.emplace(std::string_view(p->_target), std::move(p));
        }
    }

    Bridge find_path(std::string_view target) const {
        auto f = _cache_map.find(target);
        if (f != _cache_map.end()) return f->second->_bridge;
        else return nullptr;
    }

    void clear_path(std::string_view target) {
        _cache_map.erase(target);
    }

    void clear_bridge(Bridge bridge) {
        auto iter = _cache_map.begin();
        while (iter != _cache_map.end()) {
            if (iter->second->_bridge == bridge) {
                iter = _cache_map.erase(iter);
            } else {
                ++iter;
            }
        }
    }

protected:

    struct Record {
        Bridge _bridge;
        std::chrono::system_clock::time_point _expiration;
        std::string _target;
    };

    std::unordered_map<std::string_view, std::unique_ptr<Record> > _cache_map;

    std::chrono::system_clock::duration _record_timeout = std::chrono::seconds(120);

};

}

}
