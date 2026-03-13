#pragma once

template <typename Mutex> class lock_guard {
  private:
    Mutex &m_;

  public:
    explicit lock_guard(Mutex &m) : m_(m) { m_.lock(); }

    ~lock_guard() { m_.unlock(); }

    lock_guard(const lock_guard &) = delete;
    lock_guard &operator=(const lock_guard &) = delete;
};