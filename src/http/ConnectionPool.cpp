#include "http/ConnectionPool.h"
#include "log/log.h"

std::unique_ptr<HttpConnection> ConnectionPool::acquire(int sockfd, const sockaddr_in& addr, 
                                                       Router* router, RequestContext* context) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::unique_ptr<HttpConnection> conn;
    
    if (!m_free_connections.empty()) {
        // 从池中获取已有连接
        conn = std::move(m_free_connections.top());
        m_free_connections.pop();
        
        // 重新初始化连接参数
        conn->reinitialize(sockfd, addr, router, context);
        LOG_DEBUG("Reusing connection from pool, fd: %d, pool size: %zu", sockfd, m_free_connections.size());
    } else {
        // 池为空，创建新连接
        conn = std::make_unique<HttpConnection>(sockfd, addr, router, context);
        m_total_created.fetch_add(1);
        LOG_DEBUG("Created new connection, fd: %d, total created: %zu", sockfd, m_total_created.load());
    }
    
    m_in_use.fetch_add(1);
    return conn;
}

void ConnectionPool::release(std::unique_ptr<HttpConnection> conn) {
    if (!conn) {
        return;
    }
    
    // 重置连接状态
    conn->reset();
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_free_connections.size() < MAX_POOL_SIZE) {
        // 放回池中
        m_free_connections.push(std::move(conn));
        LOG_DEBUG("Connection returned to pool, pool size: %zu", m_free_connections.size());
    } else {
        // 池已满，直接销毁
        LOG_DEBUG("Pool full, destroying connection");
    }
    
    m_in_use.fetch_sub(1);
}