#pragma once

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

class string_view {
  public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    constexpr string_view() noexcept : data_(nullptr), size_(0) {}

    constexpr string_view(const char *str) : data_(str), size_(length_of(str)) {}

    constexpr string_view(const char *str, size_t count) : data_(str), size_(count) {}

    template <typename StringLike, typename = decltype(std::declval<const StringLike &>().data()),
              typename = decltype(std::declval<const StringLike &>().size()),
              std::enable_if_t<!std::is_same_v<std::decay_t<StringLike>, string_view> &&
                                   !std::is_convertible_v<const StringLike &, const char *>,
                               int> = 0>
    constexpr string_view(const StringLike &str) : data_(str.data()), size_(str.size()) {}

    constexpr const char *data() const noexcept { return data_; }

    constexpr size_t size() const noexcept { return size_; }

    constexpr size_t length() const noexcept { return size_; }

    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr const char &operator[](size_t index) const noexcept { return data_[index]; }

    constexpr const char *begin() const noexcept { return data_; }

    constexpr const char *end() const noexcept { return data_ + size_; }

    constexpr string_view substr(size_t pos, size_t count = npos) const {
        if (pos > size_) {
            throw std::out_of_range("string_view::substr");
        }

        const size_t remaining = size_ - pos;
        const size_t actual_count = count > remaining ? remaining : count;
        return string_view(data_ + pos, actual_count);
    }

    constexpr int compare(string_view other) const noexcept {
        const size_t min_size = size_ < other.size_ ? size_ : other.size_;
        for (size_t i = 0; i < min_size; ++i) {
            if (data_[i] < other.data_[i]) {
                return -1;
            }
            if (data_[i] > other.data_[i]) {
                return 1;
            }
        }

        if (size_ < other.size_) {
            return -1;
        }
        if (size_ > other.size_) {
            return 1;
        }
        return 0;
    }

    constexpr int compare(size_t pos, size_t count, string_view other) const {
        return substr(pos, count).compare(other);
    }

  private:
    static constexpr size_t length_of(const char *str) noexcept {
        if (str == nullptr) {
            return 0;
        }

        size_t len = 0;
        while (str[len] != '\0') {
            ++len;
        }
        return len;
    }

    const char *data_;
    size_t size_;
};

constexpr bool operator==(string_view lhs, string_view rhs) noexcept {
    return lhs.compare(rhs) == 0;
}

constexpr bool operator!=(string_view lhs, string_view rhs) noexcept { return !(lhs == rhs); }
