#pragma once

#include <cstddef>
#include <new>
#include <utility>

template <typename T> class deque {
  public:
    deque() : data_(nullptr), size_(0), capacity_(0), front_index_(0) {}

    deque(const deque &other) : data_(nullptr), size_(0), capacity_(0), front_index_(0) {
        copy_from(other);
    }

    deque(deque &&other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_),
          front_index_(other.front_index_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.front_index_ = 0;
    }

    deque &operator=(const deque &other) {
        if (this != &other) {
            clear();
            ::operator delete(data_);
            data_ = nullptr;
            size_ = 0;
            capacity_ = 0;
            front_index_ = 0;
            copy_from(other);
        }
        return *this;
    }

    deque &operator=(deque &&other) noexcept {
        if (this != &other) {
            clear();
            ::operator delete(data_);

            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            front_index_ = other.front_index_;

            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            other.front_index_ = 0;
        }
        return *this;
    }

    ~deque() {
        clear();
        ::operator delete(data_);
    }

    bool empty() const noexcept { return size_ == 0; }

    size_t size() const noexcept { return size_; }

    T &back() noexcept { return data_[physical_index(size_ - 1)]; }

    const T &back() const noexcept { return data_[physical_index(size_ - 1)]; }

    void push_front(const T &value) { emplace_front(value); }

    void push_front(T &&value) { emplace_front(std::move(value)); }

    template <typename... Args> void emplace_front(Args &&...args) {
        ensure_capacity_for_one_more();

        front_index_ = decrement(front_index_);
        new (data_ + front_index_) T(std::forward<Args>(args)...);
        ++size_;
    }

    void pop_back() {
        const size_t back_index = physical_index(size_ - 1);
        data_[back_index].~T();
        --size_;

        if (size_ == 0) {
            front_index_ = 0;
        }
    }

  private:
    T *data_;
    size_t size_;
    size_t capacity_;
    size_t front_index_;

    static T *allocate(size_t capacity) {
        return static_cast<T *>(::operator new(sizeof(T) * capacity));
    }

    size_t physical_index(size_t logical_index) const noexcept {
        return (front_index_ + logical_index) % capacity_;
    }

    size_t decrement(size_t index) const noexcept { return index == 0 ? capacity_ - 1 : index - 1; }

    void ensure_capacity_for_one_more() {
        if (size_ < capacity_) {
            return;
        }

        const size_t new_capacity = capacity_ == 0 ? 8 : capacity_ * 2;
        reallocate(new_capacity);
    }

    void reallocate(size_t new_capacity) {
        T *new_data = allocate(new_capacity);
        const size_t old_size = size_;

        for (size_t i = 0; i < size_; ++i) {
            new (new_data + i) T(std::move(data_[physical_index(i)]));
        }

        for (size_t i = 0; i < old_size; ++i) {
            data_[physical_index(i)].~T();
        }
        ::operator delete(data_);

        data_ = new_data;
        size_ = old_size;
        capacity_ = new_capacity;
        front_index_ = 0;
    }

    void clear() noexcept {
        for (size_t i = 0; i < size_; ++i) {
            data_[physical_index(i)].~T();
        }
        size_ = 0;
        front_index_ = 0;
    }

    void copy_from(const deque &other) {
        if (other.size_ == 0) {
            return;
        }

        data_ = allocate(other.size_);
        capacity_ = other.size_;
        front_index_ = 0;

        for (size_t i = 0; i < other.size_; ++i) {
            new (data_ + i) T(other.data_[other.physical_index(i)]);
        }

        size_ = other.size_;
    }
};
