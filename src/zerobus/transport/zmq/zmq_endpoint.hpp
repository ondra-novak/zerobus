#pragma once

#include <atomic>
#include <mutex>
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

    ///Block for receive data
    /**
     * @param identity reference to a variable that receives identity. For non-router
     * socket, this is empty
     * @param data reference to a variable that receives data
     * @param timeout specifies absolute point of timeout
     * @retval true data read
     * @retval false data didn't not read
     *
     * @note function can exit anytime sooner without any data.This
     * is caused by send() operation, which must be handled in the same thread.
     * The caller should call this function again. However the caller should
     * also check is_stopped() for external stop signal.
     *
     */
    bool receive(std::string &identity, std::vector<char> &data, std::chrono::system_clock::time_point timeout);

    ///Send data
    /**
     * @param identity identity - for non-router socket, this is ignored
     * @param data data to send
     *
     * @note function blocks if the receive thread doesn't running
     */
    void send(const std::string_view identity, std::string_view &data);

    ///send stop signal
    void stop();
    ///retrieve stop signal;
    bool is_stopped() const;


protected:
    zmq::socket_t _socket;
    int _event_fd = 0;
    int _sock_type = 0;

    std::mutex _mx_send;
    std::string_view _send_ident;
    std::string_view _send_data;
    std::atomic<bool> _send_done = {false};
    std::atomic<bool> _stop_signal = {false};

    std::mutex _mx_receive;




};

}
