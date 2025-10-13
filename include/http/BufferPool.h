#pragma once

#include <vector>
#include <stack>
#include <mutex>
#include <memory>

// 缓冲区池，用于复用缓冲区内存
class BufferPool {
public:
    static BufferPool& get_instance() {
        static BufferPool instance;
        return instance;
    }

    // 获取一个缓冲区
    std::vector<char> acquire(size_t size = 4096) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_buffers.empty()) {
            auto buffer = std::move(m_buffers.top());
            m_buffers.pop();
            buffer.clear();
            if (buffer.capacity() < size) {
                buffer.reserve(size);
            }
            return buffer;
        }
        
        std::vector<char> buffer;
        buffer.reserve(size);
        return buffer;
    }

    // 释放缓冲区回池中
    void release(std::vector<char>&& buffer) {
        if (buffer.capacity() < MIN_BUFFER_SIZE || buffer.capacity() > MAX_BUFFER_SIZE) {
            // 缓冲区太小或太大，直接丢弃
            return;
        }
        
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_buffers.size() < MAX_POOL_SIZE) {
            buffer.clear();
            m_buffers.push(std::move(buffer));
        }
    }

    size_t pool_size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_buffers.size();
    }

private:
    BufferPool() = default;
    ~BufferPool() = default;
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    mutable std::mutex m_mutex;
    std::stack<std::vector<char>> m_buffers;
    
    static constexpr size_t MAX_POOL_SIZE = 2000;    // 最大池大小
    static constexpr size_t MIN_BUFFER_SIZE = 4096; // 最小缓冲区大小
    static constexpr size_t MAX_BUFFER_SIZE = 128 * 1024; // 最大缓冲区大小
};