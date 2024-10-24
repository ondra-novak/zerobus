#pragma once
#include "network.h"
#include <coroutine>
#include <mutex>
#include <utility>

namespace zerobus {



///Helps to manipulate with opened connection
/**
 * For asynchronous reading, you can use coroutines. There is simple coroutine
 * declared named `coroutine` intended for such task. However you should
 * use better implementation for advance usage.
 */
class Stream : public IPeer {
public:

    ///To create stream, you need to call this function
    /** The stream is always created as shared_ptr object
     *
     * @param conn connection handle
     * @param ctx network context
     *
     * To obtain connection handle, use INetContext::connect or INetContext::create_server
     *
     * @return connected stream
     */
    static std::shared_ptr<Stream> create(ConnHandle conn, std::shared_ptr<INetContext> ctx) {
        auto ptr = new Stream(conn, *ctx);
        return std::shared_ptr<Stream>(ptr,[ctx](Stream *ptr){
            if (ctx->in_calback()) {
                ctx->enqueue([ptr]{delete ptr;});
            } else {
                delete ptr;
            }
        });
    }

    static constexpr std::size_t _input_buffer_size = 4096;
    static constexpr std::size_t _max_callback_lambda_size = 6*sizeof(void *);

    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;
    ~Stream() {
        _ctx.destroy(_conn);
    }

    using Clock = std::chrono::system_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;


    ///awaitable object - to support coroutine's co_await
    /**
     * @tparam RetVal return value (from co_await)
     *
     * @note actually you can use this object outside of coroutine. This
     * object is returned from the read() or write(). You can use
     * operator >> to specify a callback function which accepts just
     * return value as an argument
     *
     * @code
     * stream.read() >> [=](std::string_view data) { ... }
     * @endcode
     *
     * Note that any buffers held during asynchronous operation must not be destroyed
     *
     *
     */
    template<typename RetVal>
    class Awaitable {
    public:
        static_assert(std::is_same_v<RetVal, std::string_view> || std::is_same_v<RetVal, bool>);
        static constexpr bool read = std::is_same_v<RetVal, std::string_view>;
        static constexpr bool write  = std::is_same_v<RetVal, bool>;

        Awaitable(Stream &owner):_owner(owner) {}
        Awaitable(Stream &owner, std::string_view data):_owner(owner), _data(data) {}
        Awaitable(const Awaitable &owner) = default;
        Awaitable &operator=(const Awaitable &owner) = delete;


        ///support for coroutine co_await
        /** Don't call directly. If you must to call it directly, always
         * continue in call pattern. For example, you can call
         * await_ready() and depend on result, you must call await_suspend()
         * or await_resume().
         *
         * @retval true asynchronous operation is ready. Call await_resume to
         * retrieve result
         * @retval false asynchronous operation is not ready. Call await_suspend()
         * to register coroutine to resume later
         */
        bool await_ready() noexcept {
            return _owner.await_ready<RetVal>(_data);
        }
        ///register suspended coroutine
        /**
         * @param h suspended coroutine to be registered for async operation
         * You must call await_ready() prior this call
         */
        void await_suspend(std::coroutine_handle<> h) noexcept {
            _owner.register_coro<RetVal>(h);
        }

        ///retrieve result of finished operation
        /** This function must be called once coroutine is resumed. This is autatically
         * handled by co_await operator.
         * @return return value
         */
        RetVal await_resume() noexcept {
            return _owner.get_data<RetVal>();
        }

        ///register a callback function to retrieve result of asynchronous operation
        /**
         * @param fn function to be called once asynchronous operation is complete
         * @note if operation is already complete, the function is immediately
         * called in context of current thread. Otherwise the function is called
         * in thread where operation is completed
         */
        template<std::invocable<RetVal> Fn>
        void operator>>(Fn &&fn) {
            if (await_ready()) {
                fn(await_resume());
            } else {
                _owner.register_fn<RetVal>(std::forward<Fn>(fn));
            }
        }
    protected:
        Stream &_owner;
        std::string_view _data = {};
    };

    ///Read from stream
    /**
     * @return awaitable object returning data
     *
     * @note function should always return at least one byte length string.
     * If empty string returned, one of following conditions happened:
     *       - read timeout
     *       - end of stream
     *
     * use function is_read_timeout() to determine the first case.
     */
    Awaitable<std::string_view> read() {return {*this};}


    ///Write to stream
    /**
     * @param data data to write
     * @return awaitable object
     * @retval true write complete
     * @retval false write failed - stream closed
     */
    Awaitable<bool> write(std::string_view data) {
        return {*this, data};
    }

    ///Put back data as they would be read from the stream
    /**
     * @param data reference to data - you need to store data somewhere until
     * they are read. This is not required for data retrieved by read()
     * @note if there is awaiting coroutine, it is resumed
     *
     * @note putting back empty string has same effect as timeout
     */
    void put_back(std::string_view data) {
        _mx.lock();
        _cur_data = data;
        std::coroutine_handle<> h = std::exchange(_wt_read, {});
        if (h) h.resume(); else _mx.unlock();
    }

    ///cancel read operation
    /**
     * Cancels read operation. If there is no pending read operation, next attempt
     * will be canceled. Canceled operation is reported as timeout (is_read_timeout()
     * returns true)
     */
    void cancel_read() {
        _mx.lock();
        _read_timeouted = true;
        std::coroutine_handle<> h = std::exchange(_wt_read, {});
        if (h) h.resume(); else {
            _canceled_read = true;
            _mx.unlock();
        }
    }

    ///cancel write operation
    /**
     * @note this function cancels any writers. However by canceling
     * write cause breaking stream, (so all further writes returns false)
     */
    void cancel_write() {
        _mx.lock();
        _broken = true;
        std::coroutine_handle<> h = std::exchange(_wt_write, {});
        if (h) h.resume(); else _mx.unlock();
    }

    ///determines, whether last failed read was interrupted because timeout
    bool is_read_timeout() const {
        return _read_timeouted;
    }

    ///Sets next read timeout
    /**
     * This doesn't apply to current timeout
     * @param dur duration
     */
    void set_read_timeout(Duration dur) {
        std::lock_guard _(_mx);
        _read_timeout = dur;

    }
    ///Sets next write timeout
    /**
     * This doesn't apply to current timeout
     * @param dur duration
     */
    void set_write_timeout(Duration dur) {
        std::lock_guard _(_mx);
        _write_timeout = dur;

    }

protected:
    Stream(ConnHandle conn, INetContext &ctx)
        :_conn(conn), _ctx(ctx) {
        _ctx.ready_to_send(_conn,this);
        begin_receive();
    }


    mutable std::mutex _mx;
    ConnHandle _conn;
    INetContext &_ctx;
    char _input_buf[_input_buffer_size];
    std::string_view _cur_data;
    std::string_view _to_write = {};
    std::coroutine_handle<> _wt_write = {};
    std::coroutine_handle<> _wt_read = {};
    Duration _read_timeout = Duration::max();
    Duration _write_timeout = Duration::max();
    TimePoint _cur_read_tm = TimePoint::max();
    TimePoint _cur_write_tm = TimePoint::max();

    bool _clear_to_send = false;
    bool _broken = false;
    bool _eof = false;
    bool _reading = false;
    bool _read_timeouted = false;
    bool _canceled_read = false;

    template<typename Awaiter>
    struct CBFrame { // @suppress("Miss copy constructor or assignment operator")
        void (*_resume_fn)(std::coroutine_handle<>);
        Stream *_owner;
        char _space[_max_callback_lambda_size];

        template<typename Fn>
        void do_resume() {
            Fn *fnptr = reinterpret_cast<Fn *>(_space);
            Awaiter awt(*_owner);
            (*fnptr)(awt.await_resume());
            std::destroy_at(fnptr);
        }

        template<typename Fn>
        std::coroutine_handle<> create(Fn &&fn) {
            static_assert(sizeof(Fn) <= sizeof(_space),"To big callback function");
            Fn *fnptr = reinterpret_cast<Fn *>(_space);
            std::construct_at(fnptr, std::move(fn));
            _resume_fn = [](std::coroutine_handle<> h) {
                auto *me = reinterpret_cast<CBFrame *>(h.address());
                me->do_resume();
            };
            return std::coroutine_handle<>::from_address(this);
        }

    };
    CBFrame<Awaitable<std::string_view> > _read_fn = {nullptr, this, {}};
    CBFrame<Awaitable<bool> > _write_fn  = {nullptr, this, {}};

    template<typename RetVal>
    bool await_ready(std::string_view data) {
        if constexpr(std::is_same_v<RetVal, bool>) {
            _mx.lock();
            if (_broken) return true;
            _to_write = data;
            if (_clear_to_send) {
                return flush_write();
            }
            //unlock done in await_suspend or await_resume
            return false;
        } else if constexpr(std::is_same_v<RetVal, std::string_view>) {
            std::ignore = data;
            _mx.lock();
            _read_timeouted = false;
            if (_cur_data.empty() && !_eof && !_canceled_read) {
                begin_receive();
                return false;
            }
            _read_timeouted = _canceled_read;
            _canceled_read = false;
            return true;
        } else {
            return true;
        }
    }

    template<typename RetVal>
    void register_coro(std::coroutine_handle<> h) {
        if constexpr(std::is_same_v<RetVal, bool>) {
            charge_write_timeout();
            _wt_write = h;
        } else if constexpr(std::is_same_v<RetVal, std::string_view>) {
            charge_read_timeout();
            _wt_read = h;
        }
        _mx.unlock();
    }
    template<typename RetVal>
    RetVal get_data() {
        if constexpr(std::is_same_v<RetVal, bool>) {
            return !_broken;
            _mx.unlock();
        } else if constexpr(std::is_same_v<RetVal, std::string_view>) {
            auto r = std::exchange(_cur_data, {});
            _mx.unlock();
            return r;
        } else {
            _mx.unlock();
            return RetVal{};
        }
    }


    bool flush_write() {
        auto sz = _ctx.send(_conn, _to_write);
        if (sz == 0) {
            _broken = true;
            _to_write = {};
            return true;
        }
        _to_write = _to_write.substr(sz);
        _clear_to_send = false;
        _ctx.ready_to_send(_conn, this);
        return _to_write.empty();
    }

    virtual void clear_to_send() noexcept override {
        _mx.lock();
        if (!_to_write.empty()) {
            if (!flush_write()) {
                _mx.unlock();
                return;
            }
            else {
                _clear_to_send = true;
            }
        }
        std::coroutine_handle<> h = std::exchange(_wt_write, {});
        if (h) h.resume(); //unlock done in await_resume
        else _mx.unlock();
    }

    void begin_receive() {
        if (!_reading) {
            _ctx.receive(_conn, {_input_buf, sizeof(_input_buf)}, this);
            _reading = true;
        }
    }


    void receive_complete(std::string_view data) noexcept override {
        _mx.lock();
        _reading = false;
        _cur_data = data;
        if (data.empty()) _eof = true;
        std::coroutine_handle<> h = std::exchange(_wt_read, {});
        if (h) h.resume(); else _mx.unlock();
    }

    bool _output_allowed = false;
    bool _closed = false;
    bool _timeouted = false;

    virtual void on_timeout() noexcept override {
        while (true) {
            auto now = Clock::now();
            _mx.lock();
            if (now > _cur_read_tm) {
                _cur_read_tm = TimePoint::max();
                _read_timeouted = true;
                auto h = std::exchange(_wt_read, {});
                if (h) h.resume(); else _mx.unlock();   //await resume calls unlock
            } else if (now > _cur_write_tm) {
                _cur_write_tm = TimePoint::max();
                _broken = true;
                auto h = std::exchange(_wt_write, {});
                if (h) h.resume(); else _mx.unlock();   //await resume calls unlock
            } else {
                break;
            }
        }
        auto tp = std::min(_cur_write_tm, _cur_read_tm);
        if (tp < TimePoint::max()) {
            _ctx.set_timeout(_conn, tp, this);
        }
    }

    void charge_read_timeout() {
        auto now = Clock::now();
        if (_read_timeout < Duration::max() - now.time_since_epoch()) {
            _cur_read_tm = now + _read_timeout;
        } else {
            _cur_read_tm = TimePoint::max();
        }
        auto tp = std::min(_cur_write_tm, _cur_read_tm);
        if (tp < TimePoint::max()) {
            _ctx.set_timeout(_conn, tp, this);
        }
    }

    void charge_write_timeout() {
        auto now = Clock::now();
        if (_write_timeout < Duration::max() - now.time_since_epoch()) {
            _cur_read_tm = now + _read_timeout;
        } else {
            _cur_read_tm = TimePoint::max();
        }
        auto tp = std::min(_cur_write_tm, _cur_read_tm);
        if (tp < TimePoint::max()) {
            _ctx.set_timeout(_conn, tp, this);
        }
    }

    template<typename RetVal, typename Fn>
    void    register_fn(Fn &&fn) {
        if constexpr(std::is_same_v<RetVal, bool>) {
            _wt_write = _write_fn.create(std::forward<Fn>(fn));
            _mx.unlock();
        } else if constexpr(std::is_same_v<RetVal, std::string_view>) {
            _wt_read = _read_fn.create(std::forward<Fn>(fn));
            _mx.unlock();
        } else {
            _mx.unlock();
        }

    }

};

///Split string into two separated by a separator
/**
 * @param data input and output string. This variable changed to contains other part of the string
 * @param sep separator
 * @return first part part of original string
 *
 * @note if separator is not in the string, returns first argument and variable on
 * first argument is set to ""
 * @note if called with empty string, an empty string is returned
 */
inline constexpr std::string_view split(std::string_view &data, std::string_view sep) {
    std::string_view r;
    auto pos = data.find(sep);
    if (pos == data.npos) {
        r = data;
        data = {};
    } else {
        r = data.substr(0,pos);
        data = data.substr(pos+sep.size());
    }
    return r;
}

///Trim whitespaces at the beginning and at the end of the string
/**
 * @param data string to trim
 * @return trimmed string
 */
inline constexpr std::string_view trim(std::string_view data) {
    while (!data.empty() && isspace(data.front())) data = data.substr(1);
    while (!data.empty() && isspace(data.back())) data = data.substr(0, data.size()-1);
    return data;
}


///Fast convert upper case to lower case.
/**
 * Only works with ASCII < 128. No locales
 * @param c character
 * @return lower character
 */
inline constexpr char fast_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    else return c;
}

///Compare two strings case insensitive (ASCII only)
/**
 * @param a first string
 * @param b second string
 * @retval true strings are same (case insensitive - ASCII only)
 * @retval false not same
 */
inline constexpr bool icmp(const std::string_view &a, const std::string_view &b) {
    if (a.size() != b.size()) return false;
    std::size_t cnt = a.size();
    for (std::size_t i = 0; i < cnt; ++i) {
        if (fast_tolower(a[i]) != fast_tolower(b[i])) return false;
    }
    return true;
}

using IStrEqual = decltype([](const std::string_view &a, const std::string_view &b){
    return icmp(a,b);
});

///Compare two strings case insensitive (ASCII only)
/**
 * @param a first string
 * @param b second string
 * @retval true strings are same (case insensitive - ASCII only)
 * @retval false not same
 */
inline constexpr bool iless(const std::string_view &a, const std::string_view &b) {
    std::size_t cnt = std::min(a.size(),b.size());
    for (std::size_t i = 0; i < cnt; ++i) {
        if (fast_tolower(a[i]) != fast_tolower(b[i])) {
            return fast_tolower(a[i]) < fast_tolower(b[i]);
        }
    }
    return a.size() < b.size();
}

using IStrLess = decltype([](const std::string_view &a, const std::string_view &b){
    return iless(a,b);
});

using IStrGreater = decltype([](const std::string_view &a, const std::string_view &b){
    return iless(b,a);
});


///Parse a http header
/**
 * @param hdr header data
 * @param cb a callback function, which receives key and value for every header line
 * @return string contains first line of the header
 */
template<std::invocable<std::string_view, std::string_view> CB>
inline constexpr std::string_view parse_http_header(std::string_view hdr, CB &&cb) {
    auto first_line = split(hdr, "\r\n");
    while (!hdr.empty()) {
        auto value = split(hdr, "\r\n");
        auto key = split(value,":");
        key = trim(key);
        value = trim(value);
        cb(key, value);
    }
    return first_line;
}

///Parse http header and store result to output iterator
/**
 * @param hdr header
 * @param iter output iterator
 * @return first line
 */
template<std::output_iterator<std::pair<std::string_view, std::string_view> > Iter>
inline constexpr std::string_view parse_http_header(std::string_view hdr, Iter &iter) {
    return parse_http_header(hdr, [&](const std::string_view &a, const std::string_view &b){
       *iter += std::pair<std::string_view, std::string_view>(a,b);
    });
}

///Parse http header and store result to output iterator
/**
 * @param hdr header
 * @param iter output iterator
 * @return first line
 */
template<std::output_iterator<std::pair<std::string_view, std::string_view> > Iter>
inline constexpr std::string_view parse_http_header(std::string_view hdr, Iter &&iter) {
    return parse_http_header(hdr, iter);
}

struct HttpRequestLine {
    std::string_view method;
    std::string_view path;
    std::string_view version;
};

///Parse first request line
inline constexpr HttpRequestLine parse_http_request_line(std::string_view first_line) {
    auto m = split(first_line, " ");
    auto p = split(first_line, " ");
    auto v = first_line;
    return {m,p,v};
}



template <typename InputIt, typename OutputIt>
inline constexpr OutputIt url_encode(InputIt beg, InputIt end, OutputIt result) {
    constexpr char hex_chars[] = "0123456789ABCDEF";

    constexpr auto is_unreserved = [](char c) {
        std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_' || c == '~';
    };

    for (auto it = beg; it != end; ++it) {
        char c = *it;
        if (is_unreserved(c)) {
            *result++ = c;  // Unreserved znaky přidáme přímo
        } else {
            *result++ = '%';  // Nepovolený znak enkódujeme
            *result++ = hex_chars[(c >> 4) & 0xF];  // Vyšší 4 bity
            *result++ = hex_chars[c & 0xF];         // Nižší 4 bity
        }
    }

    return result;
}


template <typename InputIt, typename OutputIt>
inline constexpr OutputIt url_decode(InputIt beg, InputIt end, OutputIt result) {

    constexpr auto from_hex = [] (char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 2;
    };


    for (auto it = beg; it != end; ++it) {
        char c = *it;
        if (c == '%') {
            ++it;
            if (it == end) break;
            char hex1 = *it;
            ++it;
            if (it == end) break;
            char hex2 = *it;
            *result++ = (from_hex(hex1) << 4) + from_hex(hex2);
        } else {
            *result++ = c;
        }
    }

    return result;
}

class coroutine {
public:
    class promise_type {
    public:
        static constexpr std::suspend_never initial_suspend() noexcept {return {};}
        static constexpr std::suspend_never final_suspend() noexcept {return {};}
        static constexpr void return_void() {}
        static void unhandled_exception() {std::terminate();}
        coroutine get_return_object() const {return {};}
    };
};


}


