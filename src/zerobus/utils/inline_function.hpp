#pragma once
#include <algorithm>

template<typename T> class LambdaManipulator;


template<typename Ret, typename ... Args>
class LambdaManipulator<Ret(Args...)> {
public:

    struct FnTable {
        Ret (*call)(void *ptr, Args ... args);
        void (*move)(void *from , void *to);
        void (*destroy)(void *what);
    };

    template<typename Fn>
    static constexpr FnTable fntable = {
            [](void *ptr, Args ... args){
                Fn *l = static_cast<Fn *>(ptr);
                return (*l)(std::forward<Args>(args)...);
            },
            [](void *from, void *to) {
                Fn *l = static_cast<Fn *>(from);
                Fn *t = static_cast<Fn *>(to);
                std::construct_at(t, std::move(*l));
            },
            [](void *what){
                Fn *l = static_cast<Fn *>(what);
                std::destroy_at(l);
            }
    };

};

template<typename T, std::size_t sz> class InlineFunction;

template<typename Ret, typename ... Args, std::size_t sz>
class InlineFunction<Ret(Args...), sz> {
public:
    InlineFunction() = default;

    template<std::invocable<Args...> Fn>
    InlineFunction(Fn &&fn): _fntable(LambdaManipulator<Ret(Args...)>::template fntable<std::decay_t<Fn> >) {
        using FnSt = std::decay_t<Fn>;
        static_assert(sizeof(FnSt) <= sz, "Function's closure is too large");
        new(_buff) FnSt(std::move(fn));
    }

    InlineFunction(InlineFunction &&other):_fntable(other._fntable) {
        if (_fntable) _fntable->move(other._buff, _buff);
    }
    InlineFunction &operator=(InlineFunction &&other) {
        if (this != &other) {
            if (_fntable) _fntable->destroy(_buff);
            _fntable = other._fntable;
            if (_fntable) _fntable->move(other._buff, _buff);
        }
        return *this;
    }
    Ret operator()(Args ... args) {
        return _fntable->call(_buff, std::forward<Args>(args)...);
    }

    ~InlineFunction() {
        if (_fntable) _fntable->destroy(_buff);
    }


protected:
    const LambdaManipulator<Ret(Args...)>::FnTable *_fntable = nullptr;
    char _buff[sz];
};






