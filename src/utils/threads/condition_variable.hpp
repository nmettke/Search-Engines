#pragma once

#include "mutex.hpp"
#include <pthread.h>

class condition_variable {
  private:
    pthread_cond_t cond_;

  public:
    condition_variable() { pthread_cond_init(&cond_, nullptr); }

    void wait(::mutex &m) { pthread_cond_wait(&cond_, m.nativeHandle()); }

    void notify_one() { pthread_cond_signal(&cond_); }

    void notify_all() { pthread_cond_broadcast(&cond_); }

    ~condition_variable() { pthread_cond_destroy(&cond_); }
};