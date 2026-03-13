#pragma once

#include <pthread.h>

class condition_variable {
  private:
    pthread_cond_t cond_;

  public:
    condition_variable() { pthread_cond_init(&cond_, nullptr); }

    void wait(pthread_mutex_t *m) { pthread_cond_wait(&cond_, m); }

    void notify_one() { pthread_cond_signal(&cond_); }

    void notify_all() { pthread_cond_broadcast(&cond_); }

    ~condition_variable() { pthread_cond_destroy(&cond_); }
};