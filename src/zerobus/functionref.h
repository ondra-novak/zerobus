#pragma once

#include <type_traits>

namespace zerobus {

template<typename X> class FunctionRef;

template<typename Ret, typename ... Args>
class FunctionRef<Ret(Args...)> {
public:

    class ICall {
    public:

        virtual ~ICall() = default;
        virtual Ret call(Args ... args) = 0;
    };

    template<typename Fn>
    class Call: public ICall {
    public:

        Call(Fn &fn):_fn(fn) {}
        virtual Ret call(Args ... args) {return _fn(std::forward<Args>(args)...);}

    protected:
        Fn &_fn;
    };

    template<std::invocable<Args...> Fn>
    FunctionRef(Fn &&fn) {
        static_assert(std::is_convertible_v<std::invoke_result_t<Fn, Args...>, Ret>);
        using T = Call<Fn>;
        static_assert(sizeof(_buffer)>=sizeof(T));
        new(_buffer) T(fn);
    }

    ~FunctionRef() {
        std::destroy_at(get_ptr());
    }

    FunctionRef(const FunctionRef&) = delete;
    FunctionRef &operator=(const FunctionRef&) = delete;

    Ret operator()(Args ... args) {
        return get_ptr()->call(std::forward<Args>(args)...);
    }

 protected:

    struct FnTest {
        Ret operator()(Args ...);
    };
    static constexpr std::size_t need_size = sizeof(Call<FnTest>);

    char _buffer[need_size];

    ICall *get_ptr() {return reinterpret_cast<ICall *>(_buffer);}
    const ICall *get_ptr() const {return reinterpret_cast<const ICall *>(_buffer);}


};

}
