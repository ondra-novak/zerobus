#pragma once

#include <atomic>
#include <chrono>
#include <random>
#include <cstdint>



namespace zerobus {

std::size_t get_process_unique_id();


template<typename Iter>
Iter to_base62(std::uint64_t x, Iter iter, int digits = 1) {
    if (x>0 || digits>0) {
        iter = to_base62(x/62, iter, digits-1);
        auto rm = x%62;
        char c = static_cast<char>(rm < 10?'0'+rm:rm<36?'A'+rm-10:'a'+rm-36);
        *iter = c;
        ++iter;
        return iter;
    }
    return iter;
}

template<typename Iter>
static void generate_mailbox_id(Iter iter) {
    static std::atomic<std::uint64_t> counter = {0};
    std::random_device dev;
    auto rnd = dev();
    auto now = std::chrono::system_clock::now();
    auto pid = get_process_unique_id();
    iter = to_base62(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(), iter);
    iter = to_base62(pid, iter);
    iter = to_base62(counter++,iter,1);
    iter = to_base62(rnd,iter, 1);
}





}
