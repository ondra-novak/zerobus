#pragma once

#include "binary.hpp"
#include <tuple>
#include <variant>
#include <optional>
#include <string>
#include <cstring>
#include <type_traits>
#include <iterator>
#include <stdexcept>
#include <cstdint>

namespace zerobus {

namespace bin {


template<typename T>
concept CompressedUnsigned = std::is_unsigned_v<T> && sizeof(T) > 1;

template<typename T>
concept CompressedSigned = std::is_signed_v<T> && sizeof(T) > 1;

template<typename T>
concept UnsupportedTriviallyCopyable = std::is_trivially_copyable_v<T>
        && !CompressedUnsigned<T> && !CompressedSigned<T>;


template<typename T>
concept HasPushBack = requires(T a, typename T::value_type v) {
    a.push_back(v);
};

template<typename T>
concept HasInsert = requires(T a, typename T::value_type v) {
    a.insert(v);
};

template<typename Cont>
concept Container = requires(const Cont& c) {
    { c.begin() };
    { c.end() };
    typename Cont::value_type;
};





template<typename T>
struct Serializer;


template<UnsupportedTriviallyCopyable T>
struct Serializer<T> {
    template<typename Iter>
    static Iter srl(const T &obj, Iter iter) {
        const char* raw = reinterpret_cast<const char*>(&obj);
        return std::copy(raw, raw + sizeof(T), iter);
    }
    template<typename Iter>
    static T desrl(Iter &iter, Iter end) {
        if (std::distance(iter, end) < static_cast<std::ptrdiff_t>(sizeof(T)))
            throw std::bad_cast();
        T obj;
        std::copy(iter, iter + sizeof(T), reinterpret_cast<char *>(&obj));
        std::advance(iter, sizeof(T));
        return obj;
    }
};

template<CompressedUnsigned T>
struct Serializer<T> {
    template<typename Iter>
    static Iter srl(const T &v, Iter iter) {
        return encode_number(v, iter);
    }
    template<typename Iter>
    static T desrl(Iter &iter, Iter end) {
        T ret;
        iter = decode_number(ret, iter, end);
        return ret;
    }
};


template<CompressedSigned T>
struct Serializer<T> {
    template<typename Iter>
    static Iter srl(const T &v, Iter iter) {
        using U = std::make_unsigned_t<T>;
        U u = v<T(0)?(static_cast<U>(-v)<<1) + 1:static_cast<U>(v)<<1;
        return serialize(u, iter);
    }
    template<typename Iter>
    static T desrl(Iter &iter, Iter end) {
        using U = std::make_unsigned_t<T>;
        U v = deserialize<U>(iter, end);
        return (v & 1)?-static_cast<T>(v >> 1):static_cast<T>(v>>1);
    }
};

template<>
struct Serializer<std::string_view > {
    template<typename Iter>
    static Iter srl(const std::string_view &v, Iter iter) {
        iter = Serializer<std::size_t>::srl(v.size(), iter);
        for (const auto &c: v) iter = Serializer<char>::srl(c, iter);
        return iter;
    }
    static std::string_view desrl(const char *&iter, const char * end) {
        auto sz = Serializer<std::size_t>::desrl(iter, end);
        if (sz > static_cast<std::size_t>(std::distance(iter, end)))
            throw std::bad_cast();
        auto beg = iter;
        std::advance(iter, sz);
        return {beg, sz};
    }
};

template<typename T>
struct Serializer<std::basic_string<T> > {
    template<typename Iter>
    static Iter srl(const std::basic_string<T> &v, Iter iter) {
        iter = Serializer<std::size_t>::srl(v.size(), iter);
        for (const auto &c: v) iter = Serializer<char>::srl(c, iter);
        return iter;
    }
    template<typename Iter>
    static std::basic_string<T> desrl(Iter &iter, Iter end) {
        auto sz = Serializer<std::size_t>::desrl(iter, end);
        std::basic_string<T> res;
        res.resize(sz);
        for(T &x: res) x = Serializer<T>::desrl(iter, end);
        return res;
    }
};

template<typename T, typename U>
struct Serializer<std::pair<T, U> > {
    template<typename Iter>
    static Iter srl(const std::pair<T, U> &v, Iter iter) {
        iter = Serializer<T>::srl(v.first, iter);
        iter = Serializer<U>::srl(v.second, iter);
        return iter;
    }
    template<typename Iter>
    static std::pair<T, U> desrl(Iter &iter, Iter end) {
        return std::pair<T, U>(
                Serializer<T>::desrl(iter, end),
                Serializer<U>::desrl(iter, end));
    }
};

template<typename T>
struct Serializer<std::optional<T> > {
    template<typename Iter>
    static Iter srl(const std::optional<T> &v, Iter iter) {
        iter = Serializer<bool>::srl(v.has_value(), iter);
        if (v.has_value()) {
            iter =Serializer<T>::srl(*v, iter);
        }
        return iter;
    }
    template<typename Iter>
    static std::optional<T> desrl(Iter &iter, Iter end) {
        std::optional<T> res;
        if (Serializer<bool>::desrl(iter, end)) {
            res.emplace(Serializer<T>::desrl(iter, end));
        }
        return res;
    }
};

template<typename ... Ts>
struct Serializer<std::variant<Ts...> > {
    template<typename Iter>
    static Iter srl(const std::variant<Ts...> &v, Iter iter) {
        iter = Serializer<std::size_t>::srl(v.index(), iter);
        std::visit([&](const auto &x){
            using U = std::decay_t<decltype(x)>;
            iter = Serializer<U>::srl(x, iter);
        });
        return iter;
    }
    template<std::size_t N, typename Iter>
    static std::variant<Ts...> desrl(std::size_t index, Iter &iter, Iter end) {
        if constexpr(N >= sizeof...(Ts)) {
            throw std::bad_cast();
        } else {
            if (N == index) {
                return std::variant<Ts...>(std::in_place_index<N>,
                        Serializer<std::variant_alternative_t<N, std::variant<Ts...> > >
                            ::desrl(iter, end));
            } else {
                return desrl<N+1>(index, iter, end);
            }
        }
    }

    template<typename Iter>
    static std::variant<Ts...> desrl(Iter &iter, Iter end) {
        std::size_t idx = Serializer<std::size_t>::desrl(iter, end);
        return desrl<0>(idx, iter, end);
    }
};

template<typename ... Ts>
struct Serializer<std::tuple<Ts...> > {
    template<typename Iter>
    static Iter srl(const std::tuple<Ts...> &v, Iter iter) {
        std::apply([&](const auto & ... vs){
            std::initializer_list<Iter> _({
                (iter = Serializer<std::decay_t<decltype(vs)> >::srl(vs, iter))...
            });
        }, v);
        return iter;
    }

    template<typename Iter>
    static std::tuple<Ts...> desrl(Iter &iter, Iter end) {
        return std::tuple<Ts...>(Serializer<Ts>::desrl(iter, end)...);
    }
};

template<Container Cont>
struct Serializer<Cont> {

    using T = typename Cont::value_type;

    template<typename Iter>
    static Iter srl(const Cont &v, Iter iter) {
        iter = Serializer<std::size_t>::srl(v.size(), iter);
        for (const auto &c: v) iter = Serializer<T>::srl(c, iter);
        return iter;
    }
    template<typename Iter>
    requires(HasPushBack<Cont> || HasInsert<Cont>)
    static std::basic_string<T> desrl(Iter &iter, Iter end) {
        auto sz = Serializer<std::size_t>::desrl(iter, end);
        Cont res;
        for (std::size_t i = 0; i < sz; ++i) {
            if constexpr (HasPushBack<Cont>) {
                res.push_back(Serializer<T>(iter, end));
            } else if constexpr (HasInsert<Cont>){
                res.insert(Serializer<T>(iter, end));
            }
        }
        return res;
    }

};


}

}

