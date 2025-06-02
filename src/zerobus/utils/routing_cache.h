#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <string>
#include <queue>
#include <string_view>

namespace zerobus {

namespace utils {
template<typename Bridge>
class RoutingCache{
public:

    RoutingCache():_ttl(std::chrono::minutes(10))  {}

    bool register_path(std::string_view target, Bridge bridge, std::optional<std::uint32_t> rqid = std::nullopt) {
        auto e = erase_old() + _ttl;
        auto f = _cache.find(target);
        if (f != _cache.end()) {
            if (rqid) {
                if (f->second._rqid == *rqid) return false;
                f->second._rqid = *rqid;
            }
            f->second._bridge = bridge;
            f->second._expiration = e;
            _q.push({f,e});
        } else {
            auto t = strdup(target);
            auto key = std::string_view(t.get(), target.size());
            auto r = _cache.emplace(key, Record{
                std::move(t),
                std::move(bridge),
                rqid?*rqid:0U,
                e
            });
            _q.push({r.first,e});
        }
        return true;
    }

    Bridge find_path(std::string_view target) const {
        auto f = _cache.find(target);
        if (f != _cache.end()) return f->second._bridge;
        else return nullptr;
    }

    void clear_path(std::string_view target) {
        auto iter = _cache.find(target);
        if (iter != _cache.end()) {
            iter->second._bridge = nullptr;
        }
    }

    void clear_bridge(Bridge bridge) {
        auto iter = _cache.begin();
        while (iter != _cache.end()) {
            if (iter->second._bridge == bridge) {
                iter->second._bridge = nullptr;
            }
            ++iter;
        }
    }

    void set_ttl(std::chrono::system_clock::duration ttl) {
        _ttl = ttl;
    }


protected:

    struct Record {
        std::unique_ptr<char[]> _key_data;
        mutable Bridge _bridge = {};
        mutable std::uint32_t _rqid = 0;
        std::chrono::system_clock::time_point _expiration;
    };
    using Map =std::unordered_map<std::string_view, Record>;
    struct QueueItem {
        typename Map::iterator _rec;
        std::chrono::system_clock::time_point _expiration;
    };
    using Queue = std::queue<QueueItem>;
    Map _cache;
    Queue _q;


    std::chrono::system_clock::duration _ttl;

    static std::unique_ptr<char[]> strdup(std::string_view text) {
        auto r = std::make_unique<char[]>(text.size());
        std::copy(text.begin(), text.end(), r.get());
        return r;
    }

    std::chrono::system_clock::time_point erase_old() {
        auto now = std::chrono::system_clock::now();
        while  (!_q.empty() && _q.front()._expiration <= now) {
            typename Map::iterator iter =std::move(_q.front()._rec);
            _q.pop();
            if (iter->second._expiration <= now) {
                _cache.erase(iter);
            }
        }
        return now;
    }



};

}

}
