#pragma once
#include <algorithm>
#include <memory>

template<typename T> class LambdaManipulator;

///Builds constexpr tables for manipulating unknown lambdas / callable
/**
 * @tparam Ret return value of callable
 * @tparam Args arguments of callable
 */
template<typename Ret, typename ... Args>
class LambdaManipulator<Ret(Args...)> {
public:


    ///Definition of the table
    struct FnTable {
        ///use this function to call the lambda
        /**
         * @param ptr pointer to type erased space where lambda were stored (binary)
         * @param args arguments passed to the lambda
         * @return return value of the call
         */
        Ret (*call)(void *ptr, Args ... args);
        ///use this to move lambda from place to place
        /**
         * @param from pointer to current location
         * @param to pointer to new location. Ensure, that new location
         * is large enough. Use _closure_size to determine required size
         * @note the current location still must be destroyed!
         */
        void (*move)(void *from , void *to);
        ///use this to destroy lambda on current location
        /**
         * @param what pointer to current location, where the lambda function
         * is instanciated.
         *
         * @note it calls lambda's destructor. The location is
         * considered unitialized (lifetime is ended)
         *
         * @note you must always call destroy on original location
         * after move(), because move constructor doesn't automatically
         * destroy source object
         */
        void (*destroy)(void *what);
        ///contains size of closure of the lambda (total required size to store the lambda)
        std::size_t _closure_size;
        ///contains recommended aligment
        std::size_t _alignment;
    };

    ///Contains inicialized fntable for given lambda type (Fn)
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
            },
            sizeof(Fn),
            alignof(Fn)
    };

};

template<typename T>
constexpr void* to_void_ptr(T&& obj) {
    using RawT = std::remove_reference_t<T>;
    if constexpr (std::is_const_v<RawT>) {
        return const_cast<void*>(static_cast<const void*>(&obj));
    } else {
        return static_cast<void*>(&obj);
    }
}


template<typename T> class FunctionView;

template<typename Ret, typename ... Args>
class FunctionView<Ret(Args ...)> {
public:


    template<typename Fn>
    requires(std::is_invocable_r_v<Ret, Fn, Args...>)
    FunctionView(Fn &&fn)
        :_fntable(&LambdaManipulator<Ret(Args...)>::template fntable<std::decay_t<Fn>>)
        ,_ref(to_void_ptr(fn)) {}

    Ret operator()(Args ... args) const {
        return _fntable->call(_ref, std::forward<Args>(args)...);
    }

    constexpr const auto &get_fn_table() const {
        return _fntable;
    }

    void *get_data() const {
        return _ref;
    }

    struct FnDeleter {
        const typename LambdaManipulator<Ret(Args...)>::FnTable *_fntable;
        void operator()(char *c) {
            _fntable->destroy(c);
            delete [] c;

        }
    };

    template<typename Alloc>
    struct FnDeleterAlloc {
        Alloc _alloc;
        const typename LambdaManipulator<Ret(Args...)>::FnTable *_fntable;
        void operator()(char *c) {
            _fntable->destroy(c);
            _alloc.deallocate(c, _fntable->_closure_size);

        }
    };

    auto move_out() {
        char *buff = new char[](_fntable->_closure_size);
        return [ptr = std::unique_ptr<char, FnDeleter>(buff,{_fntable})]
                (Args ... args) -> Ret{
           const FnDeleter &d = ptr.get_deleter();
           return d._fntable->call(ptr.get(), std::forward<Args>(args)...);
        };
    }

    template<typename Allocator>
    auto move_out(Allocator alloc) {
        using AllocChar = typename std::allocator_traits<Allocator>::template rebind_alloc<char>;
        AllocChar char_alloc(alloc);
        char *buff = char_alloc.allocate(_fntable->_closure_size);
        return [ptr = std::unique_ptr<char, FnDeleterAlloc<AllocChar> >(buff,{char_alloc,_fntable})]
                (Args ... args) -> Ret{
           const FnDeleter &d = ptr.get_deleter();
           return d._fntable->call(ptr.get(), std::forward<Args>(args)...);
        };
    }


protected:
    const typename LambdaManipulator<Ret(Args...)>::FnTable *_fntable;
    void *_ref;
};

template<typename T, std::size_t sz> class Function;

template<typename Ret, typename ... Args, std::size_t sz>
class Function<Ret(Args...), sz> {
public:
    Function() = default;

    using Manip = LambdaManipulator<Ret(Args...)>;
    using FnTable = typename Manip::FnTable;

    template<typename Fn>
    struct Proxy {
        std::unique_ptr<Fn> _ptr;
        Ret operator()(Args &&... args) {
            return (*_ptr)(std::forward<Args>(args)...);
        }
        Proxy(Fn &&fn):_ptr(std::make_unique<Fn>(std::forward<Fn>(fn))) {}
    };

    template<typename Fn>
    using StoreType = std::conditional_t<sizeof(Fn) <= sz,
            std::decay_t<Fn>,
            Proxy<std::decay_t<Fn> > >;


    template<std::invocable<Args...> Fn>
    Function(Fn &&fn):
        _fntable(&Manip::template fntable<StoreType<Fn> >) {
        new(_buff) StoreType(std::move(fn));
    }

    Function(FunctionView<Ret(Args...)> &&view):_fntable(nullptr) {
        const FnTable *fntable = view.get_fn_table();
        if (fntable->_closure_size <= sz) {
            _fntable = fntable;
            _fntable->move(view.get_data(),_buff);
        } else {
            auto proxy = view.move_out();
            static_assert(sizeof(_buff) >= sizeof(proxy));
            new(_buff) auto(std::move(proxy));
           _fntable = &Manip::template fntable<decltype(proxy)>;
        }
    }

    Function(Function &&other):_fntable(other._fntable) {
        if (_fntable) _fntable->move(other._buff, _buff);
    }
    Function &operator=(Function &&other) {
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

    ~Function() {
        if (_fntable) _fntable->destroy(_buff);
    }


protected:
    const FnTable *_fntable = nullptr;
    char _buff[sz];
};






