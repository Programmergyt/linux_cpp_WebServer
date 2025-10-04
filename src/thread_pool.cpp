#include "thread_pool/thread_pool.h"

// 构造函数
thread_pool::thread_pool(int thread_number, int max_requests, int shutdown_timeout)
    : m_thread_number(thread_number),
      m_max_requests(max_requests),
      m_stop(false),
      m_shutdown_timeout(shutdown_timeout)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    m_threads.reserve(thread_number);
    for (int i = 0; i < thread_number; ++i)
    {
        m_threads.emplace_back(&thread_pool::run, this);
    }
}

thread_pool::~thread_pool()
{
    // 通知所有线程退出
    {
        std::unique_lock<std::mutex> lock(m_queuelocker);
        m_stop = true;
    }
    m_queuestat.notify_all(); // 唤醒所有等待的线程

    // 等待线程退出（带超时）
    for (auto &thread : m_threads)
    {
        if (thread.joinable())
        {
            // C++标准库中没有直接的超时join，但可以使用detach作为备选方案
            // 这里为了保持简单，直接使用join
            try
            {
                thread.join();
            }
            catch (const std::exception &e)
            {
                std::cerr << "Thread join error: " << e.what() << "\n";
            }
        }
    }
}

bool thread_pool::append(Task task)
{
    {
        std::unique_lock<std::mutex> lock(m_queuelocker);
        if ((int)m_workqueue.size() >= m_max_requests)
        {
            return false;
        }
        m_workqueue.push_back(task);
    }
    m_queuestat.notify_one();
    return true;
}

void thread_pool::run()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_queuelocker);

        // 等待任务或停止信号
        m_queuestat.wait(lock, [this]
                         { return m_stop || !m_workqueue.empty(); });

        // 如果收到停止信号且队列为空，退出
        if (m_stop && m_workqueue.empty())
        {
            break;
        }

        // 如果队列为空，继续等待
        if (m_workqueue.empty())
        {
            continue;
        }

        // 取出任务
        Task task = m_workqueue.front();
        m_workqueue.pop_front();
        lock.unlock();

        // 执行任务
        if (task)
            task();
    }
}
