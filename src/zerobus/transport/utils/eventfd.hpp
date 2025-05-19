#include <sys/eventfd.h>
#include <unistd.h>
#include <stdexcept>

class EventFd {
public:
    EventFd() {
        _fd = eventfd(0, EFD_NONBLOCK| EFD_CLOEXEC);
        if (_fd == -1) {
            throw std::system_error(errno, std::system_category(),"eventfd");
        }
    }

    ~EventFd() {
        if (_fd != -1) {
            close(_fd);
        }
    }

    EventFd(EventFd&& other) noexcept : _fd(other._fd) {
        other._fd = -1;
    }

    EventFd& operator=(EventFd&& other) noexcept {
        if (this != &other) {
            if (_fd != -1) {
                close(_fd);
            }
            _fd = other._fd;
            other._fd = -1;
        }
        return *this;
    }


    void set() {
        if (eventfd_write(_fd,1) == -1) {
            throw std::system_error(errno, std::system_category(), "eventfd_write");
        }
    }

    bool read_and_clear() {
        eventfd_t v = 0;
        if (eventfd_read(_fd, &v) == -1) {
            int r = errno;
            if (r == EAGAIN) return false;
            else throw std::system_error(r, std::system_category(), "eventfd_write");
        }
        return true;
    }

    int get_fd() const {
        return _fd;
    }

private:
    int _fd;
};

