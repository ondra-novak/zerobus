#pragma once

#include <sys/epoll.h>
#include <chrono>
#include <optional>

namespace zerobus {

template<typename Ident>
class EPoll {
public:

    EPoll() {
        _fd = epoll_create1(EPOLL_CLOEXEC);
        if (_fd < 0) {
            int e = errno;
            if (e != EINTR) {
                throw std::system_error(e, std::system_category(), "epoll_create1 failed");
            }
        }
    }
    ~EPoll() {
        if (_fd >= 0) ::close(_fd);
    }


    EPoll(EPoll &&x):_fd(x._fd) {x._fd = -1;}
    EPoll &operator=(EPoll &&x) {
        if (this != &x) {
            if (_fd) ::close(_fd);
            _fd = x._fd;
            x._fd = -1;
        }
        return *this;
    }

    static std::uint64_t ident_to_event_data(Ident ident) {
        static_assert(sizeof(Ident) <= sizeof(std::uint64_t) && std::is_trivially_copy_constructible_v<Ident>, "Too complex ident (max 8 bytes and must be trivially copy-able");
        auto ptr = reinterpret_cast<const std::uint64_t *>(&ident);
        return *ptr;
    }

    static Ident event_data_to_ident(std::uint64_t d) {
        static_assert(sizeof(Ident) <= sizeof(std::uint64_t) && std::is_trivially_copy_constructible_v<Ident>, "Too complex ident (max 8 bytes and must be trivially copy-able");
        auto ptr = reinterpret_cast<const Ident *>(&d);
        return *ptr;
    }

    void add(int fd, int events, Ident ident) {
        epoll_event ev;
        ev.data.u64 = ident_to_event_data(ident);
        ev.events = events;
        check_res(epoll_ctl(_fd, EPOLL_CTL_ADD,fd,&ev));
    }
    void mod(int fd, int events, Ident ident) {
        epoll_event ev;
        ev.data.u64 = ident_to_event_data(ident);
        ev.events = events;
        check_res(epoll_ctl(_fd, EPOLL_CTL_MOD,fd,&ev));
    }
    void del(int fd) {
        epoll_event ev = {};
        check_res(epoll_ctl(_fd, EPOLL_CTL_DEL,fd,&ev));
    }

    struct WaitRes { // @suppress("Miss copy constructor or assignment operator")
        std::uint32_t events;
        Ident ident;
    };

    std::optional<WaitRes> wait(std::chrono::system_clock::time_point tp = std::chrono::system_clock::time_point::max()) {
        int timeout = -1;
        if (tp < std::chrono::system_clock::time_point::max()) {
            auto now = std::chrono::system_clock::now();
            if (tp < now) timeout = 0;
            else {
                std::int64_t d = std::chrono::duration_cast<std::chrono::milliseconds>(tp - now).count();
                constexpr std::int64_t m = std::numeric_limits<int>::max();
                timeout = static_cast<int>(std::min(m,d));
            }
        }
        while (true) {
            epoll_event ev;
            int c = epoll_wait(_fd, &ev, 1, timeout);
            if (c == -1) {
                int e = errno;
                if (e != EINTR) {
                    throw std::system_error(e, std::system_category(), "epoll_wait failed");
                }
            } else if (c == 0) {
                return {};
            } else {
                return WaitRes{ev.events, event_data_to_ident(ev.data.u64)};
            }
        }
    }




protected:
    int _fd;

    void check_res(int i) {
        if (i < 0) {
            int e = errno;
            throw std::system_error(e, std::system_category(), "epoll_ctl failed");
        }
    }


};

}
