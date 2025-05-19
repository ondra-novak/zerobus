#pragma once
#include <cstddef>
#include <type_traits>

///Simple and fast hash map where handles (integers) are assigned to instance of user type
/**
 * @tparam T user type
 *
 * You can insert values. Each value receives an unique integer identifier (a handle). You
 * can find value under this handle. You can erase the value, which makes handle invalid.
 * The handle itself is never reused, so any usage of invalid handle can be easily detected
 *
 * The hashmap has O(1) complexity to access the handle, O(1) complexity to erase handle.
 * To allocate new handle the complexity is average O(1), but in rare cases O(n) when
 * re-hashing is performed
 *
 * To avoid collisions, some integers can be skipped. For example, if size of hash map
 * is 16, and handle 1 and 2 is inserted, handles 17 and 18, and others in distance of
 * size (16) will be skipped.
 *
 */
template<typename T>
class HandleHashMap {
public:

    using Handle = std::size_t;


    struct HandleDef {
        Handle _handle;
        union {
            T _value;
        };

        HandleDef():_handle(0) {}
        ~HandleDef() {
            if (_handle) std::destroy_at(&_value);
        }
        HandleDef(HandleDef &&other):_handle(other._handle) {
            if (_handle) std::construct_at(&_value, std::move(other._value));
        }
        HandleDef &operator=(HandleDef &&other) {
            if (this != &other) {
                if (_handle) std::destroy_at(&_value);
                _handle = other._handle;
                if (_handle) std::construct_at(&_value, std::move(other._value));
            }
            return *this;
        }
    };

    template<bool is_const>
    class IteratorT {
    public:

        using OwnerType = std::conditional_t<is_const, const HandleHashMap *, HandleHashMap *>;

        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = std::conditional_t<is_const, const HandleDef, HandleDef>;
        using difference_type = std::ptrdiff_t;
        using pointer = std::add_pointer_t<value_type>;
        using reference = std::add_lvalue_reference_t<value_type>;

        IteratorT(OwnerType owner, std::size_t idx):_owner(owner), _idx(idx) {}
        IteratorT(const IteratorT<!is_const> &other) requires(is_const)
                :_owner(other._owner),_idx(other._idx) {}
        bool operator==(const IteratorT &) const  = default;
        reference operator *() const {
            return _owner->_items[_idx];
        }
        pointer operator->() const {
            return &_owner->_items[_idx];
        }
        IteratorT &operator++() {
            if (_idx < _owner->_items.size()) {
                ++_idx;
                while (_idx < _owner->_items.size()) {
                    if (_owner->_items[_idx]._handle) break;
                    ++_idx;
                }
            }
            return *this;
        }
        IteratorT &operator--() {
            if (_idx > 0) {
                --_idx;
                while (_idx > 0) {
                    if (_owner->_items[_idx]._handle) break;
                    --_idx;
                }
            }
            return *this;
        }
        IteratorT operator++(int) {
            IteratorT s = *this;
            this->operator ++();
            return s;
        }
        IteratorT operator--(int) {
            IteratorT s = *this;
            this->operator --();
            return s;
        }

    protected:
        OwnerType _owner;
        std::size_t _idx;
        friend class HandleHashMap;
    };

    using Iterator = IteratorT<false>;
    using ConstIterator = IteratorT<true>;

    Iterator begin() {
        std::size_t idx = 0;
        while (idx < _items.size()) {
            if (_items[idx]._handle) break;;
            ++idx;
        }
        return Iterator(this, idx);
    }
    ConstIterator cbegin() const {
        std::size_t idx = 0;
        while (idx < _items.size()) {
            if (_items[idx]._handle) break;;
            ++idx;
        }
        return ConstIterator(this, idx);
    }
    ConstIterator begin() const {
        return cbegin();
    }

    Iterator end() {
        return Iterator(this, _items.size());
    }
    ConstIterator cend() {
        return ConstIterator(this, _items.size());
    }
    ConstIterator end() const {
        return ConstIterator(this, _items.size());
    }

    template<typename ... Args>
    requires(std::is_constructible_v<T, Args...>)
    Handle emplace(Args &&... args) {
        if (_count >= _items.size()) {
            expand();
        }
        while (true) {
            Handle h = ++_last_handle;
            auto idx = handle2index(h);
            if (_items[idx].set_if_empty(h, std::forward<Args>(args)...)) {
                ++_count;
                return h;
            }
        }
    }


    Handle insert(const T &val) requires(std::is_copy_constructible_v<T>) {
        return this->emplace(val);
    }
    Handle insert(T &&val) {
        return this->emplace(std::move(val));
    }

    Iterator find(Handle h) {
        auto idx = handle2index(h);
        if (_items[idx]._handle == h) return Iterator(this, idx);
        else return end();
    }
    ConstIterator find(Handle h) const {
        auto idx = handle2index(h);
        if (_items[idx]._handle == h) return ConstIterator(this, idx);
        else return end();
    }
    void erase(const ConstIterator &iter) {
        if (_items[iter._idx].reset()) {
            _count--;
        }
    }
    Iterator erase(Iterator iter) {
        if (_items[iter._idx].reset()) {
            _count--;
        }
        return ++iter;
    }
    bool erase(Handle h) {
        auto iter = find(h);
        if (iter != end()) {
            erase(iter);
            return true;
        } else {
            return false;
        }
    }


    HandleHashMap():_items(16) {}
    HandleHashMap(std::size_t sz): _items(sz) {}


protected:

    struct Item : HandleDef {
        template<typename ... Args>
        bool set_if_empty(Handle h, Args && ... args) {
            if (this->_handle) return false;
            std::construct_at(&this->_value, std::forward<Args>(args)...);
            this->_handle = h;
            return true;
        }

        bool reset() {
            if (this->_handle) {
                std::destroy_at(&this->_value);
                this->_handle = 0;
                return true;
            } else {
                return false;
            }
        }

    };

    static constexpr std::size_t initial_count = 16;
    std::size_t _count = 0;
    Handle _last_handle = 0;
    std::vector<Item> _items;

    std::size_t handle2index(Handle h) const {
        return h % _items.size();
    }

    void expand() {
        std::size_t newsz = _items.size() * 2;
        std::vector<Item> tmp(newsz);
        std::swap(tmp,_items);
        _count = 0;
        for(auto &x: tmp) {
            if (x._handle) {
                auto idx = handle2index(x._handle);
                _items[idx] = std::move(x);
                ++_count;
            }
        }
    }

};
