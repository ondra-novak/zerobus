#include <algorithm>
#include "channel_list_storage.hpp"
#include "message.hpp"
#include <numeric>

namespace zerobus {

struct CountingOutputIter {
    using iterator_category = std::output_iterator_tag;
    using value_type = void;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = void;

    CountingOutputIter(std::size_t &count, std::size_t &total_chars) :
            _count(count), _total_chars(total_chars) {
    }

    std::size_t &_count;
    std::size_t &_total_chars;

    CountingOutputIter& operator*() {
        return *this;
    }
    CountingOutputIter& operator=(const ChannelID &s) {
        ++_count;
        _total_chars += s.size();
        return *this;
    }

    CountingOutputIter& operator++() {
        return *this;
    }
    CountingOutputIter& operator++(int) {
        return *this;
    }
};

ChannelID* ChannelListStorage::ChannelData::items() {
    return reinterpret_cast<ChannelID*>(this + 1);
}

const ChannelID* ChannelListStorage::ChannelData::items() const {
    return reinterpret_cast<const ChannelID*>(this + 1);
}

char* ChannelListStorage::ChannelData::strings() {
    return reinterpret_cast<char*>(items() + count);
}

void ChannelListStorage::ChannelData::clear() {
    auto s = items();
    for (std::size_t i = 0; i < count; ++i)
        std::destroy_at(s + i);
    count = 0;
}

void ChannelListStorage::copy_strings() {
    ChannelID *items = _data->items();
    auto iter = _data->strings();
    for (std::size_t i = 0; i < _data->count; ++i) {
        auto iter2 = std::copy(items[i].begin(), items[i].end(), iter);
        items[i] = ChannelID(iter, iter2);
        *iter2 = '\0';
        iter = iter2;
        ++iter;
    }

}

void ChannelListStorage::alloc_items(std::size_t count, std::size_t chars) {
    std::size_t total_size = sizeof(ChannelData) + sizeof(ChannelID)*count + chars;
    if (!_data || _data->size < total_size) {
        _data.reset(reinterpret_cast<ChannelData*>(::operator new(total_size)));
        _data->size = total_size;
    } else {
        _data->clear();
    }
    _data->count = count;
}

ChannelList ChannelListStorage::store_channels(const ChannelList &lst) {

    std::size_t count = lst.size();
    std::size_t chars = 0;
    for (const ChannelID &chan: lst) {chars +=chan.size()+1;}

    alloc_items(count, chars);
    ChannelID *iter = _data->items();
    for (const auto &x : lst) {
        std::construct_at(iter, x);
        ++iter;
    }
    copy_strings();
    return get_stored();
}

ChannelList ChannelListStorage::get_stored() const {
    if (_data) {
        return ChannelList(_data->items(), _data->count);
    } else {
        return {};
    }
}

void ChannelListStorage::ChannelDataDeleter::operator()(ChannelData *ptr) {
    ptr->clear();
    std::destroy_at(ptr);
    ::operator delete(ptr);
}

ChannelList ChannelListStorage::make_ordered() {
    if (_data) {
        ChannelID *items = _data->items();
        std::sort(items, items + _data->count);
    }
    return get_stored();
}

template<typename Op>
ChannelList ChannelListStorage::set_op(const ChannelList &a, const ChannelList &b, Op &&op) {
    std::size_t count = 0;
    std::size_t chars = 0;
    op(a.begin(), a.end(), b.begin(), b.end(), CountingOutputIter(count, chars));
    if (count == 0) {
        if (_data) _data->clear();
        return {};
    }
    alloc_items(count, chars);
    op(a.begin(),  a.end(), b.begin(),  b.end(), _data->items());
    copy_strings();
    return get_stored();
}
ChannelList ChannelListStorage::set_difference(const ChannelList &a, const ChannelList &b) {
    return set_op(a,b,[](auto ... args){return std::set_difference(args...);});
}

ChannelList ChannelListStorage::set_union(const ChannelList &a, const ChannelList &b) {
    return set_op(a,b,[](auto ... args){return std::set_union(args...);});

}

}
