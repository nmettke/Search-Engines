#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>

#include "utils/vector.hpp"

template <typename T, typename Compare = std::less<T>> class PriorityQueue {
  public:
    using value_type = T;
    using size_type = std::size_t;
    using compare_type = Compare;

    PriorityQueue() = default;

    explicit PriorityQueue(const Compare &comp) : data_(), comp_(comp) {}

    [[nodiscard]] bool empty() const { return data_.size() == 0; }

    [[nodiscard]] size_type size() const { return data_.size(); }

    [[nodiscard]] size_type capacity() const { return data_.capacity(); }

    [[nodiscard]] const T &top() const {
        if (empty()) {
            throw std::out_of_range("PriorityQueue::top() called on empty queue");
        }
        return data_[0];
    }

    void push(const T &value) {
        data_.pushBack(value);
        siftUp(data_.size() - 1);
    }

    void push(T &&value) {
        data_.emplaceBack(std::move(value));
        siftUp(data_.size() - 1);
    }

    template <typename... Args> void emplace(Args &&...args) {
        data_.emplaceBack(std::forward<Args>(args)...);
        siftUp(data_.size() - 1);
    }

    void pop() {
        if (empty()) {
            throw std::out_of_range("PriorityQueue::pop() called on empty queue");
        }

        if (data_.size() == 1) {
            data_.popBack();
            return;
        }

        swap(data_[0], data_[data_.size() - 1]);
        data_.popBack();
        siftDown(0);
    }

    T extractTop() {
        if (empty()) {
            throw std::out_of_range("PriorityQueue::extractTop() called on empty queue");
        }

        T result = std::move(data_[0]);

        if (data_.size() == 1) {
            data_.popBack();
            return result;
        }

        data_[0] = std::move(data_[data_.size() - 1]);
        data_.popBack();
        siftDown(0);

        return result;
    }

    void clear() {
        while (!empty()) {
            data_.popBack();
        }
    }

  private:
    vector<T> data_;
    Compare comp_{};

    static size_type parent(size_type i) { return (i - 1) / 2; }

    static size_type leftChild(size_type i) { return 2 * i + 1; }

    static size_type rightChild(size_type i) { return 2 * i + 2; }

    bool higherPriority(const T &a, const T &b) const { return comp_(b, a); }

    void siftUp(size_type idx) {
        while (idx > 0) {
            size_type p = parent(idx);

            if (!higherPriority(data_[idx], data_[p])) {
                break;
            }

            swap(data_[idx], data_[p]);
            idx = p;
        }
    }

    void siftDown(size_type idx) {
        size_type n = data_.size();

        while (true) {
            size_type best = idx;
            size_type l = leftChild(idx);
            size_type r = rightChild(idx);

            if (l < n && higherPriority(data_[l], data_[best])) {
                best = l;
            }

            if (r < n && higherPriority(data_[r], data_[best])) {
                best = r;
            }

            if (best == idx) {
                break;
            }

            swap(data_[idx], data_[best]);
            idx = best;
        }
    }

    static void swap(T &a, T &b) {
        T tmp = std::move(a);
        a = std::move(b);
        b = std::move(tmp);
    }
};
