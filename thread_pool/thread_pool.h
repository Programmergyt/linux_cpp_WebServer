#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <list>
#include <functional>
#include <pthread.h>
#include <iostream>
#include <cstring>
#include <exception>
#include "../lock/locker.h"

class thread_pool
{ 
public:
    using Task = std::function<void()>;

    thread_pool(int thread_number = 8, int max_requests = 10000, int shutdown_timeout = 2);
    ~thread_pool();
    bool append(Task task);

private:
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number;        // 线程数
    int m_max_requests;         // 最大任务数
    pthread_t* m_threads;       // 线程数组
    std::list<Task> m_workqueue; // 任务队列
    locker m_queuelocker;       // 任务队列锁
    sem m_queuestat;            // 是否有任务需要处理
    bool m_stop;                 // 是否停止线程池
    int m_shutdown_timeout;      // 析构等待的超时时间（秒）
};

#endif