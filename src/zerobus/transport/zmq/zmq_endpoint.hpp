#pragma once

#include <atomic>
#include <mutex>
#include <queue>
#include <zmq.hpp>
#include <sys/eventfd.h>

namespace zerobus {

class ZmqEndpoint {
public:



    ZmqEndpoint(zmq::socket_t socket)
        :_socket(std::move(socket))
        ,_event_fd(eventfd(0, EFD_NONBLOCK))
        ,_sock_type(_socket.get(zmq::sockopt::type)) {

    }

    ZmqEndpoint(const ZmqEndpoint &other) = delete;
    ZmqEndpoint &operator=(const ZmqEndpoint &other) = delete;

    ~ZmqEndpoint() {
        ::close(_event_fd);
    }

    enum class RecStatus {
        message,
        error_send,
        timeout,
        stop_signal
    };

    struct Message {
        std::vector<char> identity;
        std::vector<char> data;

        std::string_view get_identity() const {
            return {identity.data(), identity.size()};
        }
        std::string_view get_data() const {
            return {data.data(), data.size()};
        }
    };

    RecStatus receive(Message &msg, std::chrono::system_clock::time_point timeout);

    void send(std::string_view data, std::string_view identity);

    ///send stop signal
    void stop();
    ///retrieve stop signal;
    bool is_stopped() const;


protected:
    struct QueueItem {
        zmq::message_t ident;
        zmq::message_t data;
    };


    zmq::socket_t _socket;
    int _event_fd = 0;
    int _sock_type = 0;


    std::mutex _mx_send;
    std::queue<QueueItem> _send_queue;
    std::atomic<bool> _stop_signal = {false};

    std::mutex _mx_receive;

    bool flush_send_queue(Message &msg);


};

}
