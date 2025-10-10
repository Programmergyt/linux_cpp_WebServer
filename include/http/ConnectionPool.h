#pragma once

#include "HttpConnection.h"
#include <memory>
#include <stack>
#include <mutex>
#include <atomic>
#include <vector>

class ConnectionPool {
public:
    static ConnectionPool& get_instance() {
        static ConnectionPool instance;
        return instance;
    }

    // 获取一个HttpConnection对象
    std::unique_ptr<HttpConnection> acquire(int sockfd, const sockaddr_in& addr, 
                                          Router* router, RequestContext* context);
    
    // 释放HttpConnection对象回池中
    void release(std::unique_ptr<HttpConnection> conn);
    
    // 获取池状态信息
    size_t pool_size() const { return m_free_connections.size(); }
    size_t total_created() const { return m_total_created.load(); }
    size_t in_use() const { return m_in_use.load(); }
    
    // 清空连接池，确保在程序退出前释放所有资源
    void clear();

private:
    ConnectionPool() = default;
    ~ConnectionPool() = default;
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    mutable std::mutex m_mutex;
    std::stack<std::unique_ptr<HttpConnection>> m_free_connections;
    std::atomic<size_t> m_total_created{0};
    std::atomic<size_t> m_in_use{0};
    
    static constexpr size_t MAX_POOL_SIZE = 10000; // 最大池大小
};

// RAII包装器，自动管理连接的获取和释放
class ManagedConnection {
public:
    ManagedConnection(int sockfd, const sockaddr_in& addr, Router* router, RequestContext* context)
        : m_conn(ConnectionPool::get_instance().acquire(sockfd, addr, router, context)) {}
    
    ~ManagedConnection() {
        if (m_conn) {
            ConnectionPool::get_instance().release(std::move(m_conn));
        }
    }
    
    // 移动构造和赋值
    ManagedConnection(ManagedConnection&& other) noexcept : m_conn(std::move(other.m_conn)) {}
    ManagedConnection& operator=(ManagedConnection&& other) noexcept {
        if (this != &other) {
            if (m_conn) {
                ConnectionPool::get_instance().release(std::move(m_conn));
            }
            m_conn = std::move(other.m_conn);
        }
        return *this;
    }
    
    // 禁用拷贝
    ManagedConnection(const ManagedConnection&) = delete;
    ManagedConnection& operator=(const ManagedConnection&) = delete;
    
    HttpConnection* get() const { return m_conn.get(); }
    HttpConnection* operator->() const { return m_conn.get(); }
    HttpConnection& operator*() const { return *m_conn; }
    
    bool valid() const { return m_conn != nullptr; }
    
    // 提前释放连接
    void release() {
        if (m_conn) {
            ConnectionPool::get_instance().release(std::move(m_conn));
        }
    }

private:
    std::unique_ptr<HttpConnection> m_conn;
};