#include "random_channel_gen.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace zerobus {

std::size_t get_process_unique_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return ::getpid();
#endif

}
}
