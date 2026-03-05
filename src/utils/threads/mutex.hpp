#pragma once

#include <pthread.h>
#include <stdexcept>

class mutex
{
private:
    pthread_mutex_t m_;

public:
    mutex()
    {
        if (pthread_mutex_init(&m_, nullptr) != 0)
            throw std::runtime_error("mutex init failed");
    }

    void lock()
    {
        pthread_mutex_lock(&m_);
    }

    void unlock()
    {
        pthread_mutex_unlock(&m_);
    }

    bool try_lock()
    {
        return pthread_mutex_trylock(&m_) == 0;
    }

    ~mutex()
    {
        pthread_mutex_destroy(&m_);
    }
};