#pragma once

#include <cstddef>

template <typename T, size_t N> struct array {
    T elements[N == 0 ? 1 : N];

    constexpr T *data() noexcept { return elements; }

    constexpr const T *data() const noexcept { return elements; }

    constexpr size_t size() const noexcept { return N; }

    constexpr bool empty() const noexcept { return N == 0; }

    constexpr T &operator[](size_t index) noexcept { return elements[index]; }

    constexpr const T &operator[](size_t index) const noexcept { return elements[index]; }

    constexpr T *begin() noexcept { return elements; }

    constexpr const T *begin() const noexcept { return elements; }

    constexpr T *end() noexcept { return elements + N; }

    constexpr const T *end() const noexcept { return elements + N; }

    constexpr T &front() noexcept { return elements[0]; }

    constexpr const T &front() const noexcept { return elements[0]; }

    constexpr T &back() noexcept { return elements[N - 1]; }

    constexpr const T &back() const noexcept { return elements[N - 1]; }
};
