#include "zmq_endpoint.hpp"

namespace zerobus {

bool ZmqEndpoint::receive(std::string &identity, std::vector<char> &data, std::chrono::system_clock::time_point timeout) {
    bool has_data = false;
    std::lock_guard _(_mx_receive);
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
    zmq::poll(items, 2, timeout_ms);
    if (items[0].revents & ZMQ_POLLIN) {
        zmq::message_t id_msg;
        zmq::message_t empty_msg;
        zmq::message_t data_msg;

        if (_sock_type == ZMQ_ROUTER) {
            std::ignore = _socket.recv(id_msg);
            std::ignore = _socket.recv(empty_msg);
            identity.clear();
            identity.append(static_cast<const char *>(id_msg.data()), id_msg.size());
        } else {
            identity.clear();
        }
        std::ignore = _socket.recv(data_msg);
        data.clear();
        data.insert(data.end(),static_cast<const char *>(data_msg.data()), static_cast<const char *>(data_msg.data())+data_msg.size());
        has_data = true;

    }

    if (items[1].revents & ZMQ_POLLIN) {
        _send_done.load(std::memory_order_acquire);
         eventfd_t buf;
         eventfd_read(_event_fd, &buf);
         if (!is_stopped())  {
             if (_sock_type == ZMQ_ROUTER) {
                 zmq::message_t id_msg(_send_ident.data(), _send_data.size());
                 zmq::message_t empty_msg(0);
                 _socket.send(id_msg, zmq::send_flags::sndmore);
                 _socket.send(empty_msg, zmq::send_flags::sndmore);
             }
             zmq::message_t data_msg(_send_data.data(), _send_data.size());
             _socket.send(data_msg, zmq::send_flags::none);
             _send_done.store(true, std::memory_order_relaxed);
         }
    }
    return has_data;
}

void ZmqEndpoint::send(const std::string_view identity, std::string_view &data) {
    std::lock_guard _(_mx_send);
    _send_ident = identity;
    _send_data = data;
    eventfd_write(_event_fd, 1);
    _send_done.wait(false);
    _send_done.store(false, std::memory_order_relaxed);
}

void ZmqEndpoint::stop() {
    _stop_signal.store(true);
    eventfd_write(_event_fd, 1);
}

bool ZmqEndpoint::is_stopped() const {
    return _stop_signal.load(std::memory_order_relaxed);
}

}
