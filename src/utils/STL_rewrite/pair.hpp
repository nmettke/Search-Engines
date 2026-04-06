#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

template <typename First, typename Second> struct pair {
    First first;
    Second second;

    constexpr pair() : first(), second() {}

    constexpr pair(const First &first_value, const Second &second_value)
        : first(first_value), second(second_value) {}

    template <typename OtherFirst, typename OtherSecond>
    constexpr pair(OtherFirst &&first_value, OtherSecond &&second_value)
        : first(std::forward<OtherFirst>(first_value)),
          second(std::forward<OtherSecond>(second_value)) {}
};

template <size_t Index, typename First, typename Second>
constexpr auto &get(pair<First, Second> &value) noexcept {
    static_assert(Index < 2, "pair index out of range");
    if constexpr (Index == 0) {
        return value.first;
    } else {
        return value.second;
    }
}

template <size_t Index, typename First, typename Second>
constexpr const auto &get(const pair<First, Second> &value) noexcept {
    static_assert(Index < 2, "pair index out of range");
    if constexpr (Index == 0) {
        return value.first;
    } else {
        return value.second;
    }
}

template <size_t Index, typename First, typename Second>
constexpr auto &&get(pair<First, Second> &&value) noexcept {
    static_assert(Index < 2, "pair index out of range");
    if constexpr (Index == 0) {
        return std::move(value.first);
    } else {
        return std::move(value.second);
    }
}

template <typename First, typename Second>
constexpr bool operator==(const pair<First, Second> &lhs, const pair<First, Second> &rhs) {
    return lhs.first == rhs.first && lhs.second == rhs.second;
}

template <typename First, typename Second>
constexpr bool operator!=(const pair<First, Second> &lhs, const pair<First, Second> &rhs) {
    return !(lhs == rhs);
}

template <typename First, typename Second>
constexpr pair<std::decay_t<First>, std::decay_t<Second>> make_pair(First &&first_value,
                                                                    Second &&second_value) {
    return pair<std::decay_t<First>, std::decay_t<Second>>(std::forward<First>(first_value),
                                                           std::forward<Second>(second_value));
}

namespace std {
template <typename First, typename Second>
struct tuple_size<::pair<First, Second>> : integral_constant<size_t, 2> {};

template <typename First, typename Second> struct tuple_element<0, ::pair<First, Second>> {
    using type = First;
};

template <typename First, typename Second> struct tuple_element<1, ::pair<First, Second>> {
    using type = Second;
};
} // namespace std
