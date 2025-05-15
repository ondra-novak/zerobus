#pragma once
#include <memory>

// Detekce podpory nativní alloca

namespace zerobus {

namespace utils {

inline std::size_t stack_reserve_max = 1024;

#if defined(__GNUC__) || defined(__clang__)
constexpr bool HAS_BUILTIN_ALLOCA=true;
#define ALLOCA(sz) __builtin_alloca(sz)
#elif defined(_MSC_VER)
constexpr bool HAS_BUILTIN_ALLOCA=true;
#include <malloc.h>
#define ALLOCA(sz) _alloca(sz)
#else
constexpr bool HAS_BUILTIN_ALLOCA=false;
#endif

constexpr std::size_t stack_size_multiplier = 16;

template<std::size_t n, std::invocable<void *> Callback>
inline auto stack_reserve_static(Callback &&cb) {
    char buff[n*stack_size_multiplier];
    return cb(static_cast<void *>(buff));
}

template<std::invocable<void *> Callback>
inline auto stack_reserve_dynamic(std::size_t sz, Callback &&cb) {
    std::unique_ptr<void,decltype([](void *x){::operator delete(x);})> _ptr(
            ::operator new(sz));
    return cb(_ptr.get());
}


template<std::invocable<void *> Callback>
inline auto stack_reserve(std::size_t sz, Callback &&cb) {
    if (sz > stack_reserve_max) {
        return stack_reserve_dynamic(sz, std::forward<Callback>(cb));
    }
    if constexpr(HAS_BUILTIN_ALLOCA) {
        return cb(ALLOCA(sz));
    } else {
        auto n = std::max<unsigned int>(sz+stack_size_multiplier-1/stack_size_multiplier,1);
        if (n <= 1) return stack_reserve_static<1>(std::forward<Callback>(cb));
        if (n <= 2) return stack_reserve_static<2>(std::forward<Callback>(cb));
        if (n <= 3) return stack_reserve_static<3>(std::forward<Callback>(cb));
        if (n <= 5) return stack_reserve_static<5>(std::forward<Callback>(cb));
        if (n <= 8) return stack_reserve_static<8>(std::forward<Callback>(cb));
        if (n <= 13) return stack_reserve_static<13>(std::forward<Callback>(cb));
        if (n <= 21) return stack_reserve_static<21>(std::forward<Callback>(cb));
        if (n <= 34) return stack_reserve_static<34>(std::forward<Callback>(cb));
        if (n <= 55) return stack_reserve_static<55>(std::forward<Callback>(cb));
        if (n <= 89) return stack_reserve_static<89>(std::forward<Callback>(cb));
        if (n <= 144) return stack_reserve_static<144>(std::forward<Callback>(cb));
        if (n <= 233) return stack_reserve_static<233>(std::forward<Callback>(cb));
        if (n <= 337) return stack_reserve_static<337>(std::forward<Callback>(cb));
        return stack_reserve_dynamic(sz, std::forward<Callback>(cb));
    }
}



template<typename T, std::invocable<T *> Callback, typename ... Args>
requires(std::is_constructible_v<T, Args...>)
auto stack_alloc(std::size_t count, Callback &&callback, Args && ... construct_args) {
    return stack_reserve(count * sizeof(T), [&](void *ptr){
        std::size_t idx = 0;
        T *new_data = reinterpret_cast<T *>(ptr);
        try {
            while (idx < count) {
                std::construct_at(new_data+idx, std::forward<Args>(construct_args)...);
                ++idx;
            }
        } catch (...) {
            while (idx > 0) {
                --idx;
                std::destroy_at(new_data+idx);
            }
            throw;
        }

        auto deleter = [count](T *r) mutable {
            while (count) {
                --count;
                std::destroy_at(r+count);
            }
        };

        std::unique_ptr<T, decltype(deleter)> hld(new_data, std::move(deleter));

        return callback(new_data);
    });
};



}
}
