#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <cstdio>
#include <list>
#include <exception>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class ThreadPool {
public:
    // thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求数量
    ThreadPool(int actor_model, ConnectionPool* connpool, 
               int thread_number = 8, int max_requests = 10000);
    ~ThreadPool();
    bool append(T* request, int state);
    bool appendNonStat(T* request);

private:
    // 工作线程执行的函数，它不断从工作队列中取出任务并执行
    static void* Worker(void* arg); // 没有this指针，所以符合线程函数规定
    void run();
private:
    int m_thread_number;        // 线程池中的线程数
    int m_max_requests;          // 请求队列中允许的最大请求数
    pthread_t* m_threads;       // 描述线程池的数组，其大小为m_thread_number
    std::list<T*> m_workqueue;  // 请求队列
    Locker m_queuelocker;       // 保护请求队列的互斥锁
    Sem m_queuestat;            // 是否有任务需要处理
    ConnectionPool* m_connpool; // 数据库
    int m_actor_model;          // 模型切换
};

template <typename T>
ThreadPool<T>::ThreadPool(int actor_model, ConnectionPool* connpool,
                          int thread_number, int max_requests)
    : m_actor_model(actor_model),
      m_thread_number(thread_number),
      m_max_requests(max_requests),
      m_threads(NULL),
      m_connpool(connpool) {
    if (thread_number <= 0 || max_requests <= 0) throw std::exception();

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) throw std::exception();

    for (int i = 0; i < thread_number; i++) {
        // 类对象传递时用this指针，传递给静态函数后，将其转换为线程池类，并调用私有成员函数run
        if (pthread_create(m_threads + i, NULL, Worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
        // 使线程的资源立即被收回,不用单独回收
        if (pthread_detach(m_threads[i]) != 0) { 
            delete [] m_threads;
            throw std::exception();
        }
    }
} 

template <typename T>
ThreadPool<T>::~ThreadPool() {
    delete [] m_threads;
}

template <typename T>
bool ThreadPool<T>::append(T* request, int state) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);

    m_queuelocker.unlock();
    m_queuestat.post(); // 信号量提醒有任务要处理
    return true;
}

template <typename T>
bool ThreadPool<T>::appendNonStat(T* request) {
        m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);

    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void* ThreadPool<T>::Worker(void* arg) {
    // 将参数强转为线程池类，调用成员方法
    ThreadPool* pool = (ThreadPool*)arg;
    pool->run();
    return pool;
}

// 工作线程从请求队列中取出某个任务进行处理
template <typename T>
void ThreadPool<T>::run() {
    while (1) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {  // 当阻塞的线程不止一个，其他需要继续等
            m_queuelocker.unlock();
            continue;
        }
        //从请求队列中取出第一个任务，将任务从请求队列删除
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request) continue;

        if (m_actor_model == 1) {
            if (request->m_state == 0) {
                if (request->readOnce()) {
                    request->improv = 1;
                    // 从连接池中取出一个数据库连接
                    ConnectionRAII mysqlcon(&request->mysql, m_connpool);
                    // process(模板类中的方法,这里是http类)进行处理
                    request->process();
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            } else {
                if (request->write()) {
                    request->improv = 1;
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else {
            ConnectionRAII mysqlcon(&request->mysql, m_connpool);
            request->process();
        }
    }
}

#endif