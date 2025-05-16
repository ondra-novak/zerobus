#pragma once

#include <string>
#include <mutex>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace zerobus
{

struct ChannelState {
    std::size_t _pos = 0;
    bool _locked = false;
};


template<typename Listener>
class Channel
{
public:
    /// default constructor
    Channel(const std::string_view &name, Listener owner = nullptr) : _name(std::move(name)), _owner(owner) {}

    /// Retrieve owner of the channel
    Listener get_owner() const { return _owner; }
    /// Retrieve channel name
    std::string_view get_name() const { return _name; }

    using State = ChannelState;

    void add(Listener lsn)
    {
        if (!is_subscribed_lk(lsn))
        {
            _listeners.push_back(lsn);
        }
    }

    bool contains(const Listener lsn) const {
        return std::find(_listeners.begin(), _listeners.end(), lsn) != _listeners.end();
    }

    bool remove(const Listener lsn)
    {
        _listeners.erase(std::remove_if(_listeners.begin(), _listeners.end(),
                                        [&](const Listener l)
                                        { return l == lsn; }),
                            _listeners.end());
        return _listeners.empty();
    }

    template<typename Message>
    void broadcast(Message &&msg, std::size_t &pos)
    {
        std::size_t cnt = _listeners.size();
        while (pos < cnt) {
            auto &p = _listeners[pos];
            ++pos;
            p->on_message(msg, false);
        }
    }

    bool empty() const
    {
        return _listeners.empty();
    }

    std::size_t size() const {
        return _listeners.size();
    }

    void close_group()
    {
        for (const auto &item : _listeners)
        {
            item->on_close_group(_name);
        }
        _listeners.clear();
    }
    ~Channel()
    {
        bool is_empty = _listeners.empty();
        if (!is_empty) {
            close_group();
        } else if (_owner) {
            _owner->on_group_empty(_name);
        }
    }

    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

    /// Creates new channel
    /**
     * @param name of the channel
     * @param owner if paramater is nullptr, public channel has been created,
     *              otherwise a group is created
     */
    static std::unique_ptr<Channel> create(const std::string_view &name, Listener owner = nullptr)
    {
        return std::make_unique<Channel>(name, owner);
    }

protected:
    std::vector<Listener > _listeners;
    std::string _name;
    Listener _owner;

    bool is_subscribed_lk(Listener lsn) const
    {
        return std::find(_listeners.begin(), _listeners.end(), lsn) != _listeners.end();
    }
};


template<typename Listener>
class PublicChannelMap: public std::unordered_map<std::string_view, std::unique_ptr<Channel<Listener> > > {
public:

    ///registers channel
    /**
     * @param name name of channel
     * @param owner if nullptr then public channel, if not nullptr, then group is
     * created.
     *
     * @return shared pointer to channel
     *
     * @note if channel is already exists, then function returns existing
     * channel. However it also checks for ownership. If ownership mismatch,
     * return value is nullptr
     *
     */
    Channel<Listener> *create_channel(std::string_view name, Listener owner = {}) {
        auto iter = this->find(name);
        if (iter != this->end()) {
            if (owner != iter->second->get_owner()) return nullptr;
            return iter->second.get();
        }
        auto ptr = Channel<Listener>::create(name, owner);
        auto r = ptr.get();
        this->emplace(r->get_name(), std::move(ptr));
        return r;
    }

    Channel<Listener> * find_channel_for_broadcast(std::string_view name, Listener sender) const {
        auto iter = this->find(name);
        if (iter == this->end() || ( iter->second->get_owner() != nullptr && sender != iter->second->get_owner())) return nullptr;
        return iter->second.get();
    }

};

template<typename Listener>
class PrivateChannelMap {
public:

    Listener  find(std::string_view channel) const {
        auto iter = _channel2listener.find(channel);
        return iter != _channel2listener.end()?iter->second:nullptr;
    }
    std::string_view find(Listener listener) {
        auto iter = _listener2channel.find(listener);
        return iter != _listener2channel.end()?iter->second:std::string_view();
    }

    bool add(std::string_view channel, Listener listener) {
        auto st = _listener2channel.emplace(listener, std::string(channel));
        if (!st.second) return false;
        std::string_view n = st.first->second;
        _channel2listener.emplace(n, listener);
        return true;
    }

    void erase(std::string_view channel) {
        auto iter = _channel2listener.find(channel);
        if (iter != _channel2listener.end()) {
            auto iter2 = _listener2channel.find(iter->second);
            _channel2listener.erase(iter);
            _listener2channel.erase(iter2);
        }
    }

    void erase(const Listener listener){
        auto iter = _listener2channel.find(listener);
        if (iter != _listener2channel.end()) {
            auto iter2 = _channel2listener.find(iter->second);
            _channel2listener.erase(iter2);
            _listener2channel.erase(iter);
        }
    }

protected:
    std::unordered_map<Listener , std::string> _listener2channel;
    std::unordered_map<std::string_view, Listener > _channel2listener;


};


}
