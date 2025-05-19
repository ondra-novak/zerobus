#pragma once
#include <optional>
#include <utility>
#include <cassert>

template<typename T, typename Deleter>
class unique_handle {
public:
    constexpr unique_handle() noexcept = default;

    constexpr unique_handle(T value, Deleter deleter)
        : value_(std::move(value)), deleter_(std::move(deleter)) {}

    constexpr unique_handle(const unique_handle&) = delete;
    constexpr unique_handle& operator=(const unique_handle&) = delete;

    constexpr unique_handle(unique_handle&& other) noexcept
        : value_(std::move(other.value_)), deleter_(std::move(other.deleter_)) {}

    constexpr unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::move(other.value_);
            deleter_ = std::move(other.deleter_);
        }
        return *this;
    }

    constexpr ~unique_handle() {
        reset();
    }

    constexpr bool has_value() const noexcept {
        return value_.has_value();
    }

    constexpr explicit operator bool() const {return static_cast<bool>(value_);}

    constexpr T& get() {
        assert(value_.has_value());
        return *value_;
    }

    constexpr const T& get() const {
        assert(value_.has_value());
        return *value_;
    }

    constexpr void reset() {
        if (value_) {
            deleter_(*value_);
            value_.reset();
        }
    }

    constexpr void reset(T new_value) {
        reset();
        value_ = std::move(new_value);
    }

private:
    std::optional<T> value_;
    Deleter deleter_;
};
