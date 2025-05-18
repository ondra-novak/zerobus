#pragma once
#include "zmq_endpoint.hpp"
#include "../../utils/multithreads.hpp"


#include <condition_variable>
namespace zerobus {



template<typename Control>
class ZmqMtHelp {
public:

    ZmqMtHelp(ZmqEndpoint &endpoint, Control &&control)
    :_endpoint(endpoint), _control(std::forward<Control>(control)) {}

    void run(unsigned int threads) {
        if (!_thread_selector.load()) {
            if (threads != 1) ++threads;
        }
        _mthr.add_threads(threads, [this](std::stop_token tkn, const bool &kf){
            if (_thread_selector.exchange(true)) {
                _have_slaves.store(true);
                worker_slave(tkn, kf);
            } else {
                worker_master(tkn, kf);
            }
        });
    }

protected:
    ZmqEndpoint &_endpoint;
    Control _control;

    std::atomic<bool> _thread_selector = {false};
    std::atomic<bool> _have_slaves = {false};
    std::mutex _qmx;
    std::condition_variable _qcond;
    std::queue<std::pair<zmq::message_t, zmq::message_t> > _rcv_queue;

    utils::MultiThread _mthr;


    void worker_slave(std::stop_token tkn, const bool &kf) {
        std::stop_callback __(tkn, [this]{
            _qcond.notify_all();
        });
        std::unique_lock lk(_qmx);
        while (!tkn.stop_requested()) {
            if (_rcv_queue.empty()) {
                _qcond.wait(lk);
            } else {
                auto m = std::move(_rcv_queue.front());
                _rcv_queue.pop();
                lk.unlock();
                _control.on_message(m.first.to_string_view(), m.second.to_string_view());
                if (kf) return;
                lk.lock();
            }
        }
    }

    void worker_master(std::stop_token tkn, const bool &kf) {
        std::stop_callback __(tkn, [this]{
            _endpoint.stop();
        });
        std::chrono::system_clock::time_point tm = {};
        while (!_endpoint.is_stopped()) {
            ZmqEndpoint::Message msg;
            auto r = _endpoint.receive(msg, tm);
            switch (r) {
                case ZmqEndpoint::RecStatus::message:
                    if (_have_slaves.load(std::memory_order_relaxed)) {{
                        std::lock_guard _(_qmx);
                        _rcv_queue.push({std::move(msg.data), std::move(msg.ident)});
                        }
                        _qcond.notify_one();
                    } else {
                        _control.on_message(msg.data.to_string_view(), msg.ident.to_string_view());
                        if (kf) return;
                    }
                    break;
                case ZmqEndpoint::RecStatus::timeout:
                    tm = _control.on_timeout();
                    if (kf) return;
                    break;
                case ZmqEndpoint::RecStatus::stop_signal:
                    return;
                case ZmqEndpoint::RecStatus::error_send:
                    _control.on_error(msg.ident.to_string_view());
                    if (kf) return;
                    break;
            }
        }


    }

};

}
