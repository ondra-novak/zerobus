#include "zmq_endpoint.hpp"

namespace zerobus {


ZmqEndpoint::RecStatus ZmqEndpoint::receive(Message &msg, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx_receive);

    while (true) {

        bool st = flush_send_queue(msg);
        if (!st) return RecStatus::error_send;

        zmq::pollitem_t items[] = {
                 { static_cast<void*>(_socket), 0, ZMQ_POLLIN, 0 },
                 { nullptr, _event_fd, ZMQ_POLLIN, 0 }
          };
        std::chrono::milliseconds timeout_ms{-1};
        if (timeout != timeout.max()) {
            timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            timeout - std::chrono::system_clock::now());
            if (timeout_ms < std::chrono::milliseconds{0}) {
                timeout_ms = std::chrono::milliseconds{0};
            }
        }
        if (zmq::poll(items, 2, timeout_ms) == 0) return RecStatus::timeout;
        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t data_msg;


            if (_sock_type == ZMQ_ROUTER) {
                zmq::message_t id_msg;
                std::ignore = _socket.recv(msg.ident);
            }
            std::ignore = _socket.recv(msg.data);
            return RecStatus::message;

        }

        if (items[1].revents & ZMQ_POLLIN) {
            if (_stop_signal.load(std::memory_order_relaxed)) return RecStatus::stop_signal;
            eventfd_t buf;
            eventfd_read(_event_fd, &buf);
            continue;
        }

    }
}

void ZmqEndpoint::send(std::string_view data, std::string_view identity) {
    std::unique_ptr<int, decltype([](int *fd){
        eventfd_write(*fd, 1);
    })> finally1(&_event_fd);

    std::lock_guard _(_mx_send);
    _send_queue.push(QueueItem{
        zmq::message_t(identity),
        zmq::message_t(data),
    });

}

void ZmqEndpoint::stop() {
    _stop_signal.store(true);
    eventfd_write(_event_fd, 1);
}

bool ZmqEndpoint::is_stopped() const {
    return _stop_signal.load(std::memory_order_relaxed);
}


bool ZmqEndpoint::flush_send_queue(Message &msg) {
    std::lock_guard _(_mx_send);
    while (!_send_queue.empty()) {
        auto &m = _send_queue.front();
        try {
            if (_sock_type == ZMQ_ROUTER) {
                msg.ident = zmq::message_t(m.ident.to_string_view());
                _socket.send(std::move(m.ident), zmq::send_flags::sndmore|zmq::send_flags::dontwait);
                _socket.send(std::move(m.data), zmq::send_flags::dontwait);
            } else if (_sock_type == ZMQ_DEALER) {
                _socket.send(std::move(m.data), zmq::send_flags::none);
            }
            _send_queue.pop();
        } catch (const zmq::error_t &) {
            _send_queue.pop();
            return false;
        }
    }
    return true;
}

}
