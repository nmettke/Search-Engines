#pragma once

#include "mutex.hpp"
#include <cerrno>
#include <cstdint>
#include <pthread.h>
#include <time.h>

class condition_variable {
  private:
    pthread_cond_t cond_;

  public:
    condition_variable() { pthread_cond_init(&cond_, nullptr); }

    void wait(mutex &m) { pthread_cond_wait(&cond_, m.nativeHandle()); }

    bool wait_until(mutex &m, std::int64_t deadlineMs) {
        timespec monoNow{};
        clock_gettime(CLOCK_MONOTONIC, &monoNow);
        std::int64_t monoNowMs = static_cast<std::int64_t>(monoNow.tv_sec) * 1000 +
                                 static_cast<std::int64_t>(monoNow.tv_nsec / 1000000);
        std::int64_t waitMs = deadlineMs - monoNowMs;
        if (waitMs <= 0) {
            return false;
        }

        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += waitMs / 1000;
        long extraNanos = static_cast<long>((waitMs % 1000) * 1000000);
        ts.tv_nsec += extraNanos;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }

        int rc = pthread_cond_timedwait(&cond_, m.nativeHandle(), &ts);
        return rc != ETIMEDOUT;
    }

    void notify_one() { pthread_cond_signal(&cond_); }

    void notify_all() { pthread_cond_broadcast(&cond_); }

    ~condition_variable() { pthread_cond_destroy(&cond_); }
};
