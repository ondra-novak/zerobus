#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <string>

namespace zerobus {

namespace utils {

template<typename Bridge>
class RoutingCache {
public:

    void set_ttl(std::chrono::system_clock::duration timeout) {
        _record_ttl = timeout;
    }

    bool register_path(std::string_view target, Bridge bridge, std::optional<std::uint32_t> rqid = std::nullopt) {
        auto f = _cache_map.find(target);
        if (f != _cache_map.end()) {
            if (rqid) {
                if (f->second->_rqid == *rqid) return false;
                f->second->_rqid = *rqid;
            }
            f->second->_bridge = bridge;
            f->second->_expiration = std::chrono::system_clock::now()+_record_ttl;
        } else {
            auto p = std::make_unique<Record>();
            p->_bridge = bridge;
            if (rqid) f->second->_rqid = *rqid;
            p->_expiration = std::chrono::system_clock::now()+_record_ttl;
            p->_target.append(target);
            _cache_map.emplace(std::string_view(p->_target), std::move(p));
        }
        return true;
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
        std::uint32_t _rqid = 0;
    };

    std::unordered_map<std::string_view, std::unique_ptr<Record> > _cache_map;

    std::chrono::system_clock::duration _record_ttl = std::chrono::seconds(300);

};

}

}
