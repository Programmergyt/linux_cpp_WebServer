#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <list>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <iostream>
#include <exception>
#include <chrono>

class ThreadPool
{
public:
    using Task = std::function<void()>;

    /**
     * @brief 构造函数，初始化线程池
     * @param thread_number 线程池中的线程数量
     * @param max_requests 任务队列的最大请求数
     * @param shutdown_timeout 析构时等待线程结束的超时时间（秒）
     */
    ThreadPool(int thread_number = 8, int max_requests = 10000, int shutdown_timeout = 2);

    ~ThreadPool();

    /**
     * @brief 向线程池添加任务
     * @param task 需要添加的任务
     * @return 成功返回 true，失败返回 false
     */
    bool append(Task task);

private:
    /** 
     * @brief 线程运行的函数
    */
    void run();

private:
    int m_thread_number;                 // 线程数
    int m_max_requests;                  // 最大任务数
    std::vector<std::thread> m_threads;  // 线程数组
    std::list<Task> m_workqueue;         // 任务队列
    std::mutex m_queuelocker;            // 任务队列锁
    std::condition_variable m_queuestat; // 条件变量，用于线程同步
    bool m_stop;                         // 是否停止线程池
    int m_shutdown_timeout;              // 析构等待的超时时间（秒）
};

#endif // THREAD_POOL_H