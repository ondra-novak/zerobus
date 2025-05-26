#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <string>


#include <queue>
using std::string_view;

#include <string_view>
namespace zerobus {

namespace utils {

template<typename Bridge>
class RoutingCache {
public:

    static constexpr std::uint8_t max_lru = 3;

    RoutingCache():_limit(100) {}

    bool register_path(std::string_view target, Bridge bridge, std::optional<std::uint32_t> rqid = std::nullopt) {
        auto f = _cache.find(target);
        if (f != _cache.end()) {
            if (rqid) {
                if (f->second._rqid == *rqid) return false;
                f->second._rqid = *rqid;
            }
            f->second._bridge = bridge;
            f->second._lru = max_lru;
        } else {
            auto t = strdup(target);
            auto key = std::string_view(t.get(), target.size());
            auto r = _cache.emplace(key, Record{
                std::move(t),
                bridge,
                rqid?*rqid:0U, max_lru
            });
            _clock.push(r.first);
        }
        return true;
    }

    Bridge find_path(std::string_view target) const {
        auto f = _cache.find(target);
        if (f != _cache.end() && f->second._lru) return f->second._bridge;
        else return nullptr;
    }

    void clear_path(std::string_view target) {
        auto iter = _cache.find(target);
        if (iter != _cache.end()) {
            iter->second._lru = 0;
        }
    }

    void clear_bridge(Bridge bridge) {
        auto iter = _cache.begin();
        while (iter != _cache.end()) {
            if (iter->second._bridge == bridge) {
                iter->second._lru = 0;
            } else {
                ++iter;
            }
        }
    }

    void set_limit(std::size_t limit) {
        _limit = limit;
    }

protected:


    struct Record {
        std::unique_ptr<char[]> _key_data;
        mutable Bridge _bridge = {};
        mutable std::uint32_t _rqid = 0;
        std::uint8_t _lru=0;
    };

    using Map =std::unordered_map<std::string_view, Record>;
    Map _cache;
    std::queue<typename Map::iterator> _clock;
    std::size_t _limit;

    static std::unique_ptr<char[]> strdup(std::string_view text) {
        auto r = std::make_unique<char[]>(text.size());
        std::copy(text.begin(), text.end(), r.get());
        return r;
    }

    void erase_old() {
        std::size_t cnt = _clock.size();
        while (cnt > _limit) {
            typename Map::iterator &iter = _clock.front();
            if (iter->second._lru > 1) {
                --iter->second._lru;
                _clock.push(std::move(iter));
                ++cnt;
            }
            _clock.pop();
            --cnt;
        }
    }

};

}

}
