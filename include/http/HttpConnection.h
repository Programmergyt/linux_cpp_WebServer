#pragma once
#include "HttpParser.h"
#include "Router.h"
#include "../tools/Tools.h" // 包含 Action
#include <vector>
#include <sys/uio.h>
#include <netinet/in.h>   // sockaddr_in, htons, htonl, INADDR_ANY 等
#include <arpa/inet.h>    // inet_pton, inet_ntop 等地址转换函数
#include <sys/socket.h>   // socket(), bind(), listen(), accept(), send(), recv() 等

class HttpConnection {
public:

    // 构造函数通过依赖注入接收它需要的组件
    HttpConnection(int sockfd, const sockaddr_in& addr, Router* router, RequestContext* context);
    ~HttpConnection();

    // 从 socket 读取并处理数据
    Action handle_read();
    
    // 向 socket 写入数据
    Action handle_write();

    // 用于在 Keep-Alive 模式下重置连接状态，准备处理下一个请求
    void reset_for_keep_alive();

    // 用于重置全部状态
    void reset();

    // 重新初始化连接（用于内存池复用）
    void reinitialize(int sockfd, const sockaddr_in& addr, Router* router, RequestContext* context);

    int get_fd() const { return m_sockfd; }

private:
    void prepare_response(const HttpResponse& response);

    int m_sockfd;
    sockaddr_in m_address;

    // 注入的依赖
    Router* m_router;
    RequestContext* m_context;

    // 内部状态
    HttpParser m_parser;
    bool m_is_writing = false;
    
    // 使用现代 C++ 的缓冲区
    std::vector<char> m_read_buffer;
    std::vector<char> m_write_buffer; // 用于存放响应头
    
    // 文件发送相关
    int m_file_fd = -1;
    off_t m_file_offset = 0;
    size_t m_file_bytes_to_send = 0;
    
    // 用于 writev
    struct iovec m_iov[2];
    int m_iov_count = 0;
    size_t m_bytes_to_send = 0;
    size_t m_bytes_have_sent = 0;

    // websocket协议升级相关
    bool m_is_websocket = false;
};