#include "http/HttpConnection.h"
#include "http/BufferPool.h"
#include "tools/tools.h"
#include "log/log.h"
#include <sys/sendfile.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sys/stat.h>
#include <fstream>
#include <sstream>

// 构造函数
HttpConnection::HttpConnection(int sockfd, const sockaddr_in& addr, Router* router, RequestContext* context)
    : m_sockfd(sockfd), m_address(addr), m_router(router), m_context(context), m_parser("/tmp") {
    // 从缓冲区池获取缓冲区
    m_read_buffer = BufferPool::get_instance().acquire(4096);
    m_write_buffer = BufferPool::get_instance().acquire(4096);
    reset();
}

HttpConnection::~HttpConnection() {
    if (m_file_fd != -1) {
        close(m_file_fd);
    }
    // 将缓冲区返回池中
    BufferPool::get_instance().release(std::move(m_read_buffer));
    BufferPool::get_instance().release(std::move(m_write_buffer));
}

// 从socket读取数据并处理
HttpConnection::Action HttpConnection::handle_read() {
    const size_t READ_CHUNK_SIZE = 4096;
    ssize_t bytes_read = 0;
    
    while (true) {
        // 确保缓冲区有足够空间
        size_t old_size = m_read_buffer.size();
        if (m_read_buffer.capacity() - old_size < READ_CHUNK_SIZE) {
            m_read_buffer.reserve(std::max(m_read_buffer.capacity() * 2, old_size + READ_CHUNK_SIZE));
        }
        m_read_buffer.resize(old_size + READ_CHUNK_SIZE);
        
        // 直接读取到缓冲区
        bytes_read = recv(m_sockfd, m_read_buffer.data() + old_size, READ_CHUNK_SIZE, 0);
        
        if (bytes_read == -1) {
            m_read_buffer.resize(old_size); // 恢复原始大小
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有更多数据可读，跳出循环
                break;
            }
            LOG_ERROR("recv error: %s", strerror(errno));
            return Action::Close;
        } else if (bytes_read == 0) {
            // 客户端关闭连接
            m_read_buffer.resize(old_size); // 恢复原始大小
            LOG_INFO("Client closed connection, fd: %d", m_sockfd);
            return Action::Close;
        }
        
        // 调整缓冲区大小为实际读取的数据大小
        m_read_buffer.resize(old_size + bytes_read);
    }
    
    if (m_read_buffer.empty()) {
        // 没有读取到数据，继续等待
        return Action::Read;
    }
    
    // 解析HTTP请求
    auto parse_result = m_parser.parse(m_read_buffer);
    
    switch (parse_result) {
        case HttpParser::ParseResult::Complete: {
            // 请求解析完成，处理请求
            const HttpRequest& request = m_parser.get_request();
            HttpResponse response = m_router->route_request(request, *m_context);
            
            // 准备响应
            prepare_response(response);
            m_is_writing = true;
            
            return Action::Write;
        }
        case HttpParser::ParseResult::Incomplete:
            // 需要更多数据
            return Action::Read;
        case HttpParser::ParseResult::Error:
            // 解析错误，发送400错误响应
            LOG_ERROR("HTTP request parse error");
            HttpResponse error_response = HttpResponse::make_error(400);
            prepare_response(error_response);
            m_is_writing = true;
            return Action::Write;
    }
    
    return Action::Read;
}

// 向socket写入数据
HttpConnection::Action HttpConnection::handle_write() {
    if (!m_is_writing) {
        return Action::Read;
    }
    
    ssize_t bytes_written = 0;
    
    while (m_bytes_to_send > 0) {
        if (m_file_fd != -1) {
            // 如果有文件需要发送，先发送响应头，再发送文件
            if (m_iov[0].iov_len > 0) {
                // 先发送响应头
                bytes_written = write(m_sockfd, m_iov[0].iov_base, m_iov[0].iov_len);
                if (bytes_written == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        return Action::Write;
                    }
                    LOG_ERROR("write header error: %s", strerror(errno));
                    close(m_file_fd);
                    m_file_fd = -1;
                    return Action::Close;
                }
                
                m_iov[0].iov_base = (char*)m_iov[0].iov_base + bytes_written;
                m_iov[0].iov_len -= bytes_written;
                m_bytes_to_send -= bytes_written;
                m_bytes_have_sent += bytes_written;
                
                if (m_iov[0].iov_len > 0) {
                    // 响应头还没发送完
                    continue;
                }
            }
            
            // 发送文件内容
            if (m_file_bytes_to_send > 0) {
                bytes_written = sendfile(m_sockfd, m_file_fd, &m_file_offset, m_file_bytes_to_send);
                if (bytes_written == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        return Action::Write;
                    }
                    LOG_ERROR("sendfile error: %s", strerror(errno));
                    close(m_file_fd);
                    m_file_fd = -1;
                    return Action::Close;
                }
                
                m_file_bytes_to_send -= bytes_written;
                m_bytes_to_send -= bytes_written;
                m_bytes_have_sent += bytes_written;
            }
        } else {
            // 发送普通响应（JSON或HTML等）
            bytes_written = writev(m_sockfd, m_iov, m_iov_count);
            
            if (bytes_written == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return Action::Write;
                }
                LOG_ERROR("writev error: %s", strerror(errno));
                return Action::Close;
            }
            
            m_bytes_have_sent += bytes_written;
            m_bytes_to_send -= bytes_written;
            
            // 更新iovec结构
            if (bytes_written >= m_iov[0].iov_len) {
                // 第一个缓冲区已经发送完毕
                bytes_written -= m_iov[0].iov_len;
                m_iov[0].iov_len = 0;
                
                if (m_iov_count > 1 && bytes_written > 0) {
                    // 更新第二个缓冲区
                    m_iov[1].iov_base = (char*)m_iov[1].iov_base + bytes_written;
                    m_iov[1].iov_len -= bytes_written;
                }
            } else {
                // 第一个缓冲区还没发送完
                m_iov[0].iov_base = (char*)m_iov[0].iov_base + bytes_written;
                m_iov[0].iov_len -= bytes_written;
            }
        }
    }
    
    // 数据发送完毕，关闭文件描述符
    if (m_file_fd != -1) {
        close(m_file_fd);
        m_file_fd = -1;
    }
    
    // 检查是否为Keep-Alive连接
    const HttpRequest& request = m_parser.get_request();
    auto connection_header = request.get_header("Connection");
    
    if (connection_header && (*connection_header == "keep-alive" || *connection_header == "Keep-Alive")) {
        // Keep-Alive连接，重置状态等待下一个请求
        reset_for_keep_alive();
        return Action::Wait;
    } else {
        // 非Keep-Alive连接，关闭连接
        return Action::Close;
    }
}

// 准备响应数据
void HttpConnection::prepare_response(const HttpResponse& response) {
    // 清空写缓冲区
    m_write_buffer.clear();
    
    // 构建响应头
    std::ostringstream header_stream;
    header_stream << "HTTP/1.1 " << response.status_code << " " << response.status_text << "\r\n";
    
    // 添加响应头
    for (const auto& [key, value] : response.headers) {
        header_stream << key << ": " << value << "\r\n";
    }
    
    // 如果是文件响应
    if (!response.file_path.empty()) {
        // 打开文件
        m_file_fd = open(response.file_path.c_str(), O_RDONLY);
        if (m_file_fd == -1) {
            LOG_ERROR("Failed to open file: %s", response.file_path.c_str());
            // 发送404错误
            HttpResponse error_response = HttpResponse::make_error(404);
            prepare_response(error_response);
            return;
        }
        
        // 获取文件大小
        struct stat file_stat;
        if (fstat(m_file_fd, &file_stat) == -1) {
            LOG_ERROR("Failed to get file stat: %s", response.file_path.c_str());
            close(m_file_fd);
            m_file_fd = -1;
            HttpResponse error_response = HttpResponse::make_error(500);
            prepare_response(error_response);
            return;
        }
        
        // 如果上层没设置 Content-Length，则自动补充
        if (response.headers.find("Content-Length") == response.headers.end()) {
            header_stream << "Content-Length: " << file_stat.st_size << "\r\n";
        }
        header_stream << "\r\n";
        
        // 准备iovec结构
        std::string header_str = header_stream.str();
        m_write_buffer.assign(header_str.begin(), header_str.end());
        
        // 确保缓冲区不为空
        if (!m_write_buffer.empty()) {
            m_iov[0].iov_base = m_write_buffer.data();
            m_iov[0].iov_len = m_write_buffer.size();
        } else {
            m_iov[0].iov_base = nullptr;
            m_iov[0].iov_len = 0;
        }
        m_iov_count = 1;
        
        m_file_offset = 0;
        m_file_bytes_to_send = file_stat.st_size;
        m_bytes_to_send = m_write_buffer.size() + file_stat.st_size;
    } else {
        // 文本响应
        if (!response.body.empty() && 
            response.headers.find("Content-Length") == response.headers.end()) {
            header_stream << "Content-Length: " << response.body.size() << "\r\n";
        }
        header_stream << "\r\n";
        
        if (!response.body.empty()) {
            header_stream << response.body;
        }
        
        std::string response_str = header_stream.str();
        m_write_buffer.assign(response_str.begin(), response_str.end());
        
        // 确保缓冲区不为空
        if (!m_write_buffer.empty()) {
            m_iov[0].iov_base = m_write_buffer.data();
            m_iov[0].iov_len = m_write_buffer.size();
        } else {
            m_iov[0].iov_base = nullptr;
            m_iov[0].iov_len = 0;
        }
        m_iov_count = 1;
        
        m_bytes_to_send = m_write_buffer.size();
    }
    
    m_bytes_have_sent = 0;
}

// 重置连接状态以支持Keep-Alive
void HttpConnection::reset_for_keep_alive() {
    m_parser.reset();
    m_read_buffer.clear();
    m_write_buffer.clear();
    m_is_writing = false;
    
    if (m_file_fd != -1) {
        close(m_file_fd);
        m_file_fd = -1;
    }
    
    m_file_offset = 0;
    m_file_bytes_to_send = 0;
    
    // 清零iovec结构
    memset(m_iov, 0, sizeof(m_iov));
    m_iov_count = 0;
    m_bytes_to_send = 0;
    m_bytes_have_sent = 0;
}

// 完全重置连接状态
void HttpConnection::reset() {
    reset_for_keep_alive();
}

// 重新初始化连接（用于内存池复用）
void HttpConnection::reinitialize(int sockfd, const sockaddr_in& addr, Router* router, RequestContext* context) {
    // 关闭旧的文件描述符（如果有）
    if (m_file_fd != -1) {
        close(m_file_fd);
        m_file_fd = -1;
    }
    
    // 设置新的连接参数
    m_sockfd = sockfd;
    m_address = addr;
    m_router = router;
    m_context = context;
    
    // 重置所有状态
    reset();
    
    // 清空缓冲区但保持容量
    m_read_buffer.clear();
    m_write_buffer.clear();
}
