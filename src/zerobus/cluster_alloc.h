

#pragma once
#include <memory>
#include <array>
#include <atomic>

namespace zerobus {

///Allocate from cluster pool
/**
 * @tparam T type
 * @tparam _cluster_size size of cluster, default value 0 means automatic (4096 bytes)
 * @tparam atomic_release set true to allow atomic release, this allows to use
 * allocator in multiple threads for deallocate only. Useful in producent-consument pattern
 */
template<typename T, unsigned int _cluster_size = 0, bool atomic_release = false>
class ClusterAlloc {
public:


    static constexpr auto _cs = _cluster_size?_cluster_size:4096/sizeof(T);

    using value_type = T;

    template< class U >
    struct rebind {
        typedef ClusterAlloc<U, _cluster_size, atomic_release> other;
    };

    ClusterAlloc() = default;
    ClusterAlloc(ClusterAlloc &&other) = default;
    ClusterAlloc &operator=(ClusterAlloc &&other) = default;
    ClusterAlloc(const ClusterAlloc &) {}

    template<typename X>
    ClusterAlloc(const ClusterAlloc<X,_cluster_size, atomic_release> &) {}




    T *allocate(int n) {
        if (n>1) {
            return reinterpret_cast<T *>(::operator new(sizeof(T)*n));
        }
        if (!_first_free.load()) {
            alloc_cluster();
        }
        Item *p = _first_free.load();
        Item *nx = p->next_free;
        while (!_first_free.compare_exchange_weak(p, nx)) {
            p = nx;
            nx = p->next_free;
        }

        return reinterpret_cast<T *>(p);
    }

    void deallocate(T *ptr, int n) {
        if (n>1) {
            ::operator delete(ptr);
            return;
        }

        Item *x = reinterpret_cast<Item *>(ptr);
        x->next_free = _first_free.load();
        while (!_first_free.compare_exchange_weak(x->next_free,x));
    }

protected:



    union Item { // @suppress("Miss copy constructor or assignment operator")
        Item * next_free;
        char mem[sizeof(T)];
    };

    struct Cluster {
        std::unique_ptr<Cluster> _next;
        std::array<Item, _cs> _data;
    };

    struct LastFreePtr { // @suppress("Miss copy constructor or assignment operator")
        Item *ptr;
        Item *load() const {return ptr;}
        bool compare_exchange_weak(Item * &e, Item *n) {
            if (e == ptr) {
                ptr = n;
                return true;
            } else {
                e = ptr;
                return false;
            }
        }
    };

    using Ptr = std::conditional_t<atomic_release,std::atomic<Item *>,LastFreePtr>;

    std::unique_ptr<Cluster> _clusters;
    Ptr _first_free = {};


    void alloc_cluster() {
        auto c = std::make_unique<Cluster>();
        for (auto &z: c->_data) {
            z.next_free = _first_free.load();
            while (!_first_free.compare_exchange_weak(z.next_free, &z));
        }
        c->_next = std::move(_clusters);
        _clusters = std::move(c);
    }
};

}

