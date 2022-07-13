#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>
#include <exception>

/*
 * 用Sem对象来管理一个未命名信号量，默认inline
 */
class Sem {
public:
    Sem() {
        if (sem_init(&m_sem, 0, 0) != 0)  throw std::exception();
    }

    Sem(int num) {
        if (sem_init(&m_sem, 0, num) != 0)  throw std::exception();
    }

    ~Sem() {
        sem_destroy(&m_sem);
    }

    bool wait() {
        return sem_wait(&m_sem) == 0;    // 与原来返回值不同，成功返回1，出错返回0 
    }

    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

/*
 * 用Locker对象来管理一个互斥量
 */
class Locker {
public:
    Locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)  throw std::exception();
    }

    ~Locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;    // 与原来返回值不同，成功返回1，出错返回0
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;   
    }

    pthread_mutex_t* get() {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

/*
 * 用Cond对象来管理一个条件变量
 */
class Cond {
public:
    Cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0)  throw std::exception();
    }

    ~Cond() {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t* m_mutex) {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;    // 与原来返回值不同，成功返回1，出错返回0
    }

    bool timedwait(pthread_mutex_t* m_mutex, const struct timespec* t) {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, t);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    // static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif