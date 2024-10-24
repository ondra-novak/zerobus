#pragma once
#include "network.h"
#include <coroutine>
#include <mutex>
#include <utility>

namespace zerobus {

class Stream : public IPeer {
public:

    Stream(ConnHandle conn, std::shared_ptr<INetContext> ctx, std::string_view initial_data);
    Stream(ConnHandle conn, std::shared_ptr<INetContext> ctx);
    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;
    ~Stream() {
        _ctx->destroy(_conn);
    }

    struct WakeUpHandler {
        void operator()(void *ptr) {
            std::coroutine_handle<>::from_address(ptr).resume();
        }
    };

    using Suspended = std::unique_ptr<void, WakeUpHandler>;


    class ReadAwaitable {
    public:
        ReadAwaitable(Stream &owner):_owner(owner) {}
        bool await_ready() const noexcept {
            return _owner.read_await_ready();
        }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            return _owner.read_register(h);
        }
        std::string_view await_resume() noexcept {
            return _owner.read_get_data();
        }
    private:
        Stream &_owner;
        friend class Stream;
    };

    class WriteAwaitable {
    public:
        WriteAwaitable(Stream &owner, bool processed):_owner(owner),_processed(processed) {}
        bool await_ready() const noexcept {
            return _processed;
        }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            return _owner.write_register(h);
        }
        bool await_resume() noexcept {
            return _owner.get_write_result();
        }

    private:
        Stream &_owner;
        bool _processed;
        friend class Stream;

    };


    ReadAwaitable read() {
        std::lock_guard _(_mx);
        return ReadAwaitable(*this);
    }

    WriteAwaitable write(std::string_view data) {
        std::lock_guard _(_mx);
        if (_closed) return {*this, true};
        if (_output_allowed) {
            auto count = _ctx->send(_conn, data);
            if (count == data.size()) return {*this,true};
            _to_write = data.substr(count);
            _output_allowed = false;
        }
        return {*this,false};
    }

    Suspended put_back(std::string_view data) {
        std::lock_guard _(_mx);
        _cur_data = data;
        return std::move(_wt_read);
    }


    bool is_read_timeout() const {
        std::lock_guard _(_mx);
        return _timeouted;
    }

protected:
    mutable std::mutex _mx;
    ConnHandle _conn;
    std::shared_ptr<INetContext> _ctx;
    std::string_view _cur_data;
    char _input_buf[4192];

    std::string_view _to_write;
    Suspended _wt_read = {};
    Suspended _wt_write = {};
    std::optional<std::chrono::system_clock::duration> _rd_timeout;
    std::optional<std::chrono::system_clock::duration> _wr_timeout;

    std::chrono::system_clock::time_point _read_timeout = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point _write_timeout = std::chrono::system_clock::time_point::max();

    bool _output_allowed = false;
    bool _closed = false;
    bool _timeouted = false;

    friend class ReadAwaitable;
    friend class WriteAwaitable;

    bool read_await_ready() const {
        std::lock_guard _(_mx);
        return !_cur_data.empty() || _closed;
    }
    bool read_register(std::coroutine_handle<> h) {
        std::lock_guard _(_mx);
        if (!_cur_data.empty() || _closed) return false;
        _wt_read = Suspended(h.address());
        _ctx->receive(_conn, {_input_buf, sizeof(_input_buf)}, this);
        if (_rd_timeout) charge_read_timeout(*_rd_timeout);
        return true;
    }
    std::string_view read_get_data() {
        std::lock_guard _(_mx);
        return std::exchange(_cur_data, {});
    }

    bool write_register(std::coroutine_handle<> h) {
        std::lock_guard _(_mx);
        if (_output_allowed || _closed) return false;
        if (_wr_timeout) charge_read_timeout(*_wr_timeout);
        _wt_write = Suspended(h.address());
        return true;
    }
    bool get_write_result() {
        std::lock_guard _(_mx);
        return !_closed;
    }

    virtual void receive_complete(std::string_view data) noexcept override {
        Suspended s;
        std::lock_guard _(_mx);
        _cur_data = data;
        if (data.empty()) _closed = true;
        s = std::move(_wt_read);

    }
    virtual void clear_to_send() noexcept override {
        Suspended s;
        std::lock_guard _(_mx);
        if (_to_write.empty()) {
            _output_allowed = true;
            s = std::move(_wt_write);
        } else {
            auto cnt = _ctx->send(_conn, _to_write);
            if (cnt == 0) {
                _closed = true;
                s = std::move(_wt_write);
            } else {
                _to_write = _to_write.substr(cnt);
                _ctx->ready_to_send(_conn, this);
            }
        }

    }
    virtual void on_timeout() noexcept override {
        Suspended rd,wr;
        std::lock_guard _(_mx);
        auto tp  = std::chrono::system_clock::now();
        if (tp <=_read_timeout) {
            _timeouted = true;
            rd = std::move(_wt_read);
            _read_timeout = _read_timeout.max();
        }
        if (tp <= _write_timeout) {
            wr = std::move(_wt_write);
            _write_timeout = _write_timeout.max();
            _closed = true;
        }
        auto tm = std::min(_read_timeout, _write_timeout);
        if (tm < tm.max()) {
            _ctx->set_timeout(_conn, tm, this);
        }

    }

    void charge_read_timeout(std::chrono::system_clock::duration dur) {
        std::lock_guard _(_mx);
        _read_timeout = std::chrono::system_clock::now() + dur;
        _ctx->set_timeout(_conn, std::min(_read_timeout, _write_timeout), this);
    }

    void charge_write_timeout(std::chrono::system_clock::duration dur) {
        std::lock_guard _(_mx);
        _write_timeout = std::chrono::system_clock::now() + dur;
        _ctx->set_timeout(_conn,std::min(_read_timeout, _write_timeout), this);
    }

};


}

