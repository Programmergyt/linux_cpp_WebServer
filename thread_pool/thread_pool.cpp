#include "thread_pool.h"

// 构造函数
thread_pool::thread_pool(int thread_number, int max_requests, int shutdown_timeout)
    : m_thread_number(thread_number), 
    m_max_requests(max_requests), 
    m_threads(nullptr), 
    m_stop(false),
     m_shutdown_timeout(2) 
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();

    for (int i = 0; i < thread_number; ++i) {
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        // 不要 detach，析构时要 join
    }
}

thread_pool::~thread_pool() {
    // 通知所有线程退出
    m_stop = true;
    for (int i = 0; i < m_thread_number; ++i) {
        m_queuestat.post(); // 唤醒等待的线程，让它们进入run然后直接退出
    }

    // 等待线程退出（带超时）
    for (int i = 0; i < m_thread_number; ++i) {
        void* retval = nullptr;

        // 设置超时时间：当前时间 + 2 秒
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += m_shutdown_timeout;  // 最多等 2 秒

        int ret = pthread_timedjoin_np(m_threads[i], &retval, &ts);
        if (ret == 0) {
            // 成功等待线程退出
            // std::cout << "Thread " << m_threads[i] << " exited gracefully\n";
        } else if (ret == ETIMEDOUT) {
            // 超时未退出
            std::cout << "Thread " << m_threads[i] << " did not exit in time\n";
        } else {
            // 其他错误
            std::cerr << "pthread_timedjoin_np error: " << strerror(ret) << "\n";
        }
    }

    delete[] m_threads;
}

bool thread_pool::append(Task task) {
    m_queuelocker.lock();
    if ((int)m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(task);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

void* thread_pool::worker(void* arg) {
    thread_pool* pool = (thread_pool*)arg;
    pool->run();
    return pool;
}

void thread_pool::run() {
    while (true) {
        m_queuestat.wait();
        // 如果析构时有线程正等待，它们会收到m_queuestat信号，进入循环，然后直接退出不会执行
        if (m_stop) {
            break;
        }
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        Task task = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (task) task();
    }
}
