#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <string_view>

namespace zerobus {

namespace bin {


template<typename T>
requires(std::is_unsigned_v<T>)
std::size_t get_encoded_number_size(T number) {
    if (number < 0x1F) return 1;
    int bits = std::bit_width(number);
    return 1 + (bits + 7) / 8;
}


template<typename T, typename OutputIter>
requires(std::is_unsigned_v<T>)
OutputIter encode_number(T number, OutputIter iter) {
    std::size_t sz = get_encoded_number_size(number)-1;
    if (sz > 7) return encode_number(0x1FFFFFFFFFFFFFFFULL, iter);
    std::uint8_t b = (number & 0x1F) | ((sz & 0x7)<<5);
    *iter = b;
    ++iter;
    if (sz) [[unlikely]] {
        number >>= 5;
        while (sz) {
            *iter = static_cast<uint8_t>(number & 0xFF);
            ++iter;
            --sz;
            number >>= 8;
        }
    }
    return iter;
}

template<typename T, typename Iter>
requires(std::is_unsigned_v<T>)
Iter decode_number(T &number, Iter from, Iter to) {
    number = 0;
    if (from == to) [[unlikely]] return from;
    std::uint8_t b = static_cast<std::uint8_t>(*from);
    ++from;
    number = b & 0x1F;
    if (b > 0x1F) [[unlikely]] {
        unsigned int count = b >> 5;
        for (unsigned int i = 0; i < count; ++i) {
            if (from == to) [[unlikely]] break;
            T tmp = static_cast<std::uint8_t>(*from);
            tmp <<= (i * 8 + 5);
            ++from;
            number |= tmp;
        }
    }
    return from;
}

std::size_t get_encoded_string_size(std::string_view sz) {
    return get_encoded_number_size(sz.size())+sz.size();
}

template<typename OutputIter>
OutputIter encode_string(std::string_view text, OutputIter iter) {
    iter = encode_number(text.size(), iter);
    return std::copy(text.begin(), text.end(), iter);
}

inline const char *decode_string(std::string_view &text, const char *from, const char *to) {
    std::size_t sz;
    from = decode_number(sz, from, to);
    sz = std::min<std::size_t>(sz, std::distance(from,to));
    text = std::string_view(from, sz);
    from += sz;
    return from;
}




}


}
