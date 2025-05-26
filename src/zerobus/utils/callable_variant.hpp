#pragma once

#include <variant>

template<typename Fn, typename ... Ts>
class CallableVariant;

template<typename Ret, typename ... Ts, typename ... Args>
class CallableVariant<Ret(Args ...), Ts ...>: public std::variant<Ts...> {
public:
    using std::variant<Ts...>::variant;

    Ret operator()(Args ... args) {
        return std::visit([&](auto &subj){
            return subj(std::forward<Args>(args)...);
        },*this);
    }
    Ret operator()(Args ... args) const {
        return std::visit([&](const auto &subj){
            return subj(std::forward<Args>(args)...);
        });
    }


};
