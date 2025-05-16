#pragma once
#include <cstddef>
#include <stdexcept>

namespace zerobus {

namespace utils {


template<typename T>
class DispatchQueue;


template<typename Ret, typename ... Args>
class DispatchQueue<Ret(Args...)> {
public:

    DispatchQueue()=default;
    DispatchQueue(std::size_t default_slot_size):_fragment_size(default_slot_size) {}
    DispatchQueue(const DispatchQueue &) = delete;
    DispatchQueue &operator=(const DispatchQueue &) = delete;
    DispatchQueue(DispatchQueue &&o)
        :_first(o._first)
        ,_last(o._last)
        ,_cache(o._cache)
        ,_fragment_size(o._fragment_size)
        ,_cur_item(o._cur_item)
        {
            o._first = o._last = o._cache = nullptr;
            o._cur_item = nullptr;


        }
    DispatchQueue &operator=(DispatchQueue &&o) {
        if (this != &o) {
            std::destroy_at(this);
            std::construct_at(this, std::move(o));
        }
        return *this;
    }

    template<typename Fn>
    void push(Fn &&fn) {
        push_internal(std::forward<Fn>(fn));
    }

    Ret pop(Args ... args) {
        return pop_internal(std::forward<Args>(args)...);
    }
    Ret front(Args ... args) {
        return front_internal(std::forward<Args>(args)...);
    }

    Ret pop_rv(Args &&... args) {
        return pop_internal(std::forward<Args>(args)...);
    }
    Ret front_rv(Args &&... args) {
        return front_internal(std::forward<Args>(args)...);
    }

    bool pop_discard() {
        return discard_internal();
    }


    bool empty() const {
        return _first == nullptr || _first->top == _first->bottom;
    }


    ~DispatchQueue() {
        while (!empty()) {
            discard_internal();
        }
        delete _first;
        delete _cache;
    }


protected:

    class ICommon {
    public:
        virtual std::size_t size() const = 0;
        virtual Ret call(Args && ... args) = 0;
        virtual ~ICommon() = default;
    };


    struct QItem {
        QItem *next;
        std::size_t bottom;
        std::size_t top;
        std::size_t size;
    };


    QItem *_first = nullptr;
    QItem *_last = nullptr;
    QItem *_cache = nullptr;
    std::size_t _fragment_size = 1024;
    ICommon *_cur_item = nullptr;

    template<typename Fn>
    class Item : public ICommon {
    public:
        Item(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
        Item(const Item &) = delete;
        Item &operator=(const Item &) = delete;
        virtual std::size_t size() const {return sizeof(Item);}
        virtual Ret call(Args && ... args) {return _fn(std::forward<Args>(args)...);}
    protected:
        Fn _fn;
    };

    QItem *alloc_fragment(std::size_t minsize) {
        if (_cache && _cache->size >= minsize) {
            auto r = _cache;
            _cache = nullptr;
            return r;
        }
        std::size_t alloc_size = std::max(_fragment_size,minsize);
        QItem *out = reinterpret_cast<QItem *>(::operator new(sizeof(QItem)+alloc_size));
        out->bottom = 0;
        out->top = 0;
        out->size = alloc_size;
        out->next = nullptr;
        return out;
    }


    template<typename Fn>
    void push_internal(Fn &&fn) {
        if (_last == nullptr) {
            _first=_last=alloc_fragment(sizeof(Item<Fn>));
        } else if (_last->size-_last->top < sizeof(Item<Fn>)) {
            auto f = alloc_fragment(sizeof(Item<Fn>));
            _last->next = f;
            _last = f;
        }
        void *top = reinterpret_cast<char *>(_last+1) + _last->top;
        _last->top += sizeof(Item<Fn>);
        new(top) Item<Fn>(std::forward<Fn>(fn));
    }

    void cleanup() {
        if (_cur_item) {
            std::size_t sz = _cur_item->size();
            std::destroy_at(_cur_item);
            _cur_item = nullptr;
            _first->bottom+=sz;
            if (_first->bottom == _first->top) {
                if (_first != _last) {
                    delete _cache;
                    _cache = _first;
                    _cache->bottom = _cache->top = 0;
                    _first = _first->next;
                } else {
                    _first->bottom = _first->top = 0;
                }
            }
        }
    }

    Ret front_internal(Args &&... args) {
        if (empty()) throw std::logic_error("Empty DispatchQueue");
        ICommon *top = reinterpret_cast<ICommon *>(
                reinterpret_cast<char *>(_first+1) + _first->bottom);

        return top->call(std::forward<Args>(args)...);
    }

    Ret pop_internal(Args &&... args) {
        if (empty()) throw std::logic_error("Empty DispatchQueue");
        ICommon *top = reinterpret_cast<ICommon *>(
                reinterpret_cast<char *>(_first+1) + _first->bottom);
        _cur_item = top;

        std::unique_ptr<DispatchQueue, decltype([](DispatchQueue *me){
            me->cleanup();
        })> _(this);

        return top->call(std::forward<Args>(args)...);
    }

    bool discard_internal() {
        if (empty()) return false;
        ICommon *top = reinterpret_cast<ICommon *>(
                reinterpret_cast<char *>(_first+1) + _first->bottom);
        _cur_item = top;
        cleanup();
        return true;
    }

};


}

}
