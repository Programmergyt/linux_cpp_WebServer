#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <mutex>
#include <condition_variable>
#include <vector>

template <class T>
class block_queue {
public:

    explicit block_queue(size_t max_size = 1000)
        : m_front(0), m_back(0), m_array(max_size),m_max_size(max_size), m_size(0)  {
        if (max_size <= 0) {
            throw std::invalid_argument("max_size must be greater than 0");
        }
    }

    ~block_queue() = default;

    // 禁止拷贝和赋值
    block_queue(const block_queue&) = delete;
    block_queue& operator=(const block_queue&) = delete;

    // push: 队列满时阻塞等待
    void push(const T& item) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_not_full.wait(lk, [this] { return m_size < m_max_size; });

        m_array[m_back] = item;
        m_back = (m_back + 1) % m_max_size;
        ++m_size;

        lk.unlock();
        m_not_empty.notify_one();
    }

    // pop: 队列空时阻塞等待
    void pop(T& item) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_not_empty.wait(lk, [this] { return m_size > 0; });

        item = m_array[m_front];
        m_front = (m_front + 1) % m_max_size;
        --m_size;

        lk.unlock();
        m_not_full.notify_one();
    }

    // 带超时 pop，返回是否成功
    bool pop(T& item, int ms_timeout) {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (!m_not_empty.wait_for(lk, std::chrono::milliseconds(ms_timeout), [this] { return m_size > 0; })) {
            return false; // 超时
        }

        item = m_array[m_front];
        m_front = (m_front + 1) % m_max_size;
        --m_size;

        lk.unlock();
        m_not_full.notify_one();
        return true;
    }

    bool empty() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_size == 0;
    }

    bool full() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_size == m_max_size;
    }

    size_t size() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_size;
    }

    size_t max_size() {
        return m_max_size;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_size = 0;
        m_front = m_back = 0;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_not_empty;
    std::condition_variable m_not_full;

    size_t m_front;
    size_t m_back;
    std::vector<T> m_array;
    size_t m_max_size;
    size_t m_size;
    
};

#endif
