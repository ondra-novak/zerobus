#pragma once

struct HybridUniquePtrDeleter {
    bool _owned;
    template<typename T>
    constexpr void operator()(T *x) {if (_owned) delete x;}
    HybridUniquePtrDeleter(bool owned):_owned(owned) {}
};


template<typename T> using HybridUniquePtr = std::unique_ptr<T,HybridUniquePtrDeleter>;
