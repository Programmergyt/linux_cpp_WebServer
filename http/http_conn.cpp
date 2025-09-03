#include "http_conn.h"
#include <stdarg.h>
#include <sys/sendfile.h>

static const char* ok_200_title       = "OK";
static const char* error_400_title    = "Bad Request";
static const char* error_400_form     = "Your request has bad syntax or is inherently impossible to satisfy.\n";
static const char* error_403_title    = "Forbidden";
static const char* error_403_form     = "You do not have permission to get file from this server.\n";
static const char* error_404_title    = "Not Found";
static const char* error_404_form     = "The requested file was not found on this server.\n";
static const char* error_500_title    = "Internal Error";
static const char* error_500_form     = "There was an unusual problem serving the requested file.\n";
int http_conn::m_epoll_fd = -1; // 或其他合适的初始值

http_conn::http_conn()
{
    m_sockfd = -1;
    m_CONNTrigmode = 0;
    m_close_log = 0;
    m_actor_model = 0;
    m_io_state = IO_NONE;
    m_check_state = CHECK_STATE_REQUESTLINE;

    m_host = nullptr;
    m_version = nullptr;
    m_content_length = 0;
    m_linger = false;
    m_connPool = nullptr;
    m_doc_root = nullptr;

    m_file_address = nullptr;
    memset(&m_file_stat, 0, sizeof(m_file_stat));

    memset(m_url, 0, sizeof(m_url));
    memset(m_content_type, 0, sizeof(m_content_type));

    m_read_idx = m_checked_idx = m_start_line = 0;
    m_write_idx = 0;
    m_body_index = 0;
    m_iv_count = 0;
    m_bytes_to_send = m_bytes_have_sent = 0;
}

http_conn::~http_conn()
{
    unmap_file();
}

// 只重置解析/缓冲状态，不动 sockfd 等
void http_conn::init()
{
     // 解析状态
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_io_state = IO_NONE;

    // 读缓冲索引
    m_read_idx   = 0;
    m_checked_idx= 0;
    m_start_line = 0;
    m_body_index = 0;

    // 写缓冲索引
    m_write_idx  = 0;

    // 本次请求字段
    m_method = GET;
    m_url[0] = '\0';
    m_version = nullptr;
    m_host = nullptr;
    m_content_length = 0;
    m_content_type[0] = '\0';
    m_content[0] = '\0';

    // 分散写与计数
    m_iv_count = 0;
    m_bytes_to_send = 0;
    m_bytes_have_sent = 0;

    // 按你的实现，是否长连接由新请求头决定，这里重置为 false 更稳妥
    m_linger = false;

    // 清理上次可能映射的文件
    unmap_file();
}

// 全部初始化
void http_conn::init(int sockfd,const sockaddr_in &addr, char *root, int CONNTrigmode, int close_log)
{
    m_sockfd = sockfd;
    m_address = addr;
    m_doc_root = root;
    m_CONNTrigmode = CONNTrigmode;
    m_close_log = close_log;
    m_actor_model = 0; // 默认 Proactor 模型

    // 重置解析状态
    m_host = nullptr;
    m_version = nullptr;
    m_content_length = 0;
    m_linger = false;
    memset(m_url, 0, sizeof(m_url));
    memset(m_content_type, 0, sizeof(m_content_type));
    memset(m_content, 0, sizeof(m_content));

    m_read_idx = m_checked_idx = m_start_line = 0;
    m_write_idx = 0;
    m_iv_count = 0;
    m_body_index = 0;
    m_bytes_to_send = m_bytes_have_sent = 0;
    m_io_state = IO_NONE;
    m_check_state = CHECK_STATE_REQUESTLINE; // <-- 确保每次连接都从头开始

    unmap_file();
}

void http_conn::close_conn(bool real_close)
{
    if (real_close && m_sockfd != -1) {
        close(m_sockfd);
        m_sockfd = -1;
    }
    unmap_file();
}

// 返回true表示单次读成功，返回false表示读失败可以退出
bool http_conn::read_once()
{
    if (m_sockfd == -1) return false;
    // LT: 读一次就返回；ET: 循环读取直到 EAGAIN
    if (m_CONNTrigmode == 0) {
        int bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read <= 0) return false;
        m_read_idx += bytes_read;
        return true;
    } else {
        while (true) {
            int bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                return false;
            } else if (bytes_read == 0) {
                return false; // 对端关闭
            }
            m_read_idx += bytes_read;
            if (m_read_idx >= READ_BUFFER_SIZE) break; // 满了就退出，读不全大文件
        }
        return true;
    }
}

// 返回true表示单次写成功，返回false表示写失败可以退出
bool http_conn::write_once()
{
    if (m_sockfd == -1) return false;
    LOG_DEBUG("sockfd:%d, m_iv_count:%d,write iv[0]: %s", m_sockfd, m_iv_count ,m_write_buf); // 调试输出
    if(m_iv_count==2)
        LOG_DEBUG("sockfd:%d, write iv[1] file_address: %p", m_sockfd ,m_file_address); // 调试输出 
    while (true) 
    {
        ssize_t bytes = writev(m_sockfd, m_iv, m_iv_count);
        LOG_DEBUG("writev bytes: %zd", bytes); // 调试输出
        if (bytes <= 0) {
            if (bytes == 0) {
                LOG_DEBUG("sockfd:%d, writev returned 0, peer likely closed connection", m_sockfd);
            } else {  // bytes == -1
                if (errno == EAGAIN) {
                    // ET 下要继续等待 EPOLLOUT；LT 也同理
                    Tools::modfd(m_epoll_fd, m_sockfd, EPOLLOUT, m_CONNTrigmode);
                    LOG_DEBUG("EAGAIN, wait for next EPOLLOUT");
                    return true; // 下次再写
                }
                LOG_ERROR("sockfd:%d, writev error: %s", m_sockfd, strerror(errno));
            }
            unmap_file();
            return false;
        }
        m_bytes_have_sent += bytes;
        m_bytes_to_send   -= bytes;
        if (m_bytes_have_sent >= (size_t)m_iv[0].iov_len) {
            // 头部已发完，移动到文件段
            LOG_DEBUG("HTTP header sent completely");
            m_iv[0].iov_len = 0;
            size_t sent_in_file = m_bytes_have_sent - (size_t)m_write_idx; // 已经在文件上发送的字节
            if (m_iv_count == 2) {
                if (sent_in_file < (size_t)m_iv[1].iov_len) {
                    m_iv[1].iov_base = (char*)m_file_address + sent_in_file;
                    m_iv[1].iov_len  = m_iv[1].iov_len - sent_in_file;
                } else {
                    // 文件也发完
                    m_iv[1].iov_len = 0;
                }
            }
        } else {
            // 头还没发完
            LOG_DEBUG("Partial HTTP header sent");
            m_iv[0].iov_base = (char*)m_iv[0].iov_base + bytes;
            m_iv[0].iov_len  = m_iv[0].iov_len - bytes;
        }
        if (m_bytes_to_send <= 0) 
        {
            // 全部发送结束
            LOG_DEBUG("All data sent");
            unmap_file();
            init();
            // m_io_state = IO_NONE;
            if (m_linger) {return true;}   // 继续保持连接 
            else {return false;}  // 让上层关闭连接
        }
        LOG_DEBUG("bytes_have_sent: %zu, bytes_to_send: %zu", m_bytes_have_sent, m_bytes_to_send);
    }
}

void http_conn::process()
{
    if (m_actor_model == 1) { // Reactor
        if (m_io_state == IO_READ) {
            HTTP_CODE read_ret = process_read();
            if (read_ret == NO_REQUEST) {
                // 继续监听读事件
                Tools::modfd(m_epoll_fd, m_sockfd, EPOLLIN, m_CONNTrigmode);
                return;
            }
            bool write_ok = process_write(read_ret);
            if (!write_ok) {
                close_conn();
                return;
            }
            // 🚀 已经生成响应，切换到写事件
            Tools::modfd(m_epoll_fd, m_sockfd, EPOLLOUT, m_CONNTrigmode);

        } else if (m_io_state == IO_WRITE) {
            // Reactor 下线程可能负责写
            if (!write_once()) {
                close_conn();
            }
        }

    } else { // Proactor
        HTTP_CODE read_ret = process_read();
        if (read_ret == NO_REQUEST) {
            // 没读全，继续等
            Tools::modfd(m_epoll_fd, m_sockfd, EPOLLIN, m_CONNTrigmode);
            return;
        }

        bool write_ok = process_write(read_ret);
        if (!write_ok) {
            close_conn();
            return;
        }

        // 🚀 响应已经准备好，切换到写事件
        Tools::modfd(m_epoll_fd, m_sockfd, EPOLLOUT, m_CONNTrigmode);
    }
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        char ch = m_read_buf[m_checked_idx];
        if (ch == '\r') {
            if (m_checked_idx + 1 == m_read_idx) return LINE_OPEN;
            if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (ch == '\n') {
            // 宽松处理：接受单独 '\n'
            m_read_buf[m_checked_idx++] = '\0';
            return LINE_OK;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // method SP url SP version
    // 简单解析
    char *url = strpbrk(text, " \t");
    LOG_DEBUG("sockfd:%d, parse_request_line: %s",m_sockfd, text);
    if (!url) return BAD_REQUEST;
    *url++ = '\0';

    if (strcasecmp(text, "GET") == 0) m_method = GET;
    else if (strcasecmp(text, "POST") == 0) m_method = POST;
    else if (strcasecmp(text, "HEAD") == 0) m_method = HEAD;
    else return BAD_REQUEST;

    url += strspn(url, " \t");
    char *version = strpbrk(url, " \t");
    if (!version) return BAD_REQUEST;
    *version++ = '\0';
    version += strspn(version, " \t");

    m_version = version;
    if (strncasecmp(m_version, "HTTP/1.1", 8) != 0) return BAD_REQUEST;

    if (strncasecmp(url, "http://", 7) == 0) {
        url += 7;
        url = strchr(url, '/'); // 跳到 path
    } else if (strncasecmp(url, "https://", 8) == 0) {
        url += 8;
        url = strchr(url, '/');
    }
    if (!url || url[0] != '/') return BAD_REQUEST;

    // 默认首页
    if (strlen(url) == 1) {
        strcpy(m_url, "/index.html");
    } else {
        strncpy(m_url, url, sizeof(m_url)-1);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    LOG_DEBUG("sockfd:%d, parse_headers: %s",m_sockfd, text);
    if (text[0] == '\0') {
        // 头部结束，进入 body
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            m_body_index = m_checked_idx; // body 从这里开始
            return NO_REQUEST;  // 继续读 body
        }
        return GET_REQUEST; // 无 body，直接完成
    } 
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11; text += strspn(text, " \t");
        if (strncasecmp(text, "keep-alive", 10) == 0) m_linger = true;
    } 
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15; text += strspn(text, " \t");
        m_content_length = atol(text);
    } 
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5; text += strspn(text, " \t");
        m_host = text;
    } 
    else if (strncasecmp(text, "Content-Type:", 13) == 0) {
        text += 13; text += strspn(text, " \t");
        strncpy(m_content_type, text, sizeof(m_content_type)-1);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // text 是 body 开始位置，have是已经读到的总字节数
    int have = m_read_idx - (text - m_read_buf);
    if (have >= m_content_length) {
        // 已经读全
        int copy_len = std::min<long>(m_content_length, sizeof(m_content) - 1);
        memcpy(m_content, text, copy_len);
        m_content[copy_len] = '\0';
        LOG_DEBUG("sockfd:%d, parse_content OK, body=%s", m_sockfd, m_content);
        return GET_REQUEST;
    }
    return NO_REQUEST; // 还需要继续读
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = nullptr;

    while (true) {
        if (m_check_state == CHECK_STATE_CONTENT) {
            // 专门处理 POST body
            text = m_read_buf + m_body_index;
            ret = parse_content(text);
            if (ret == GET_REQUEST) {//GET_REQUEST表示已经完整读完请求
                m_check_state = CHECK_STATE_REQUESTLINE;
                return do_request();
            }
            return NO_REQUEST;
        }

        // 先解析一行
        line_status = parse_line();
        if (line_status == LINE_OPEN) return NO_REQUEST;
        if (line_status == LINE_BAD) return BAD_REQUEST;

        text = get_line();
        m_start_line = m_checked_idx;

        switch (m_check_state) 
        {
        case CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST) return BAD_REQUEST;
            m_check_state = CHECK_STATE_HEADER;
            break;
        case CHECK_STATE_HEADER:
            ret = parse_headers(text);
            if (ret == BAD_REQUEST) return BAD_REQUEST;
            if (ret == GET_REQUEST) {
                m_check_state = CHECK_STATE_REQUESTLINE;
                return do_request();
            }
            break;
        default:
            return BAD_REQUEST;
        }
    }
}

http_conn::HTTP_CODE http_conn::do_request()
{
    // 处理用户接口
    if (m_method == POST && strcmp(m_url, "/api/register") == 0) {
        // 解析 body: username, password, email
        std::string body(m_content);
        std::string username, password, email;
        // 简单解析 key=value&key=value
        auto parseForm = [](const std::string& body, const char* key) {
            size_t pos = body.find(std::string(key) + "=");
            if (pos == std::string::npos) return std::string();
            size_t end = body.find("&", pos);
            return body.substr(pos + strlen(key) + 1, end - pos - strlen(key) - 1);
        };
        username = parseForm(body, "username");
        password = parseForm(body, "password");
        email    = parseForm(body, "email");

        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, m_connPool);
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "INSERT INTO users(username,password,email) VALUES('%s','%s','%s')",
                 username.c_str(), password.c_str(), email.c_str());
        if (mysql_query(mysql, sql)) {
            m_content_length = snprintf(m_content, sizeof(m_content),
                "{ \"status\": \"error\", \"msg\": \"注册失败\" }");
        } else {
            m_content_length = snprintf(m_content, sizeof(m_content),
                "{ \"status\": \"ok\", \"msg\": \"注册成功\" }");
        }
        return GET_REQUEST; // 交给 process_write 返回 JSON
    }

    if (m_method == POST && strcmp(m_url, "/api/login") == 0) {
        std::string body(m_content);
        std::string username, password;
        auto parseForm = [](const std::string& body, const char* key) {
            size_t pos = body.find(std::string(key) + "=");
            if (pos == std::string::npos) return std::string();
            size_t end = body.find("&", pos);
            return body.substr(pos + strlen(key) + 1, end - pos - strlen(key) - 1);
        };
        username = parseForm(body, "username");
        password = parseForm(body, "password");
        LOG_DEBUG("Login attempt: username=%s, password=%s", username.c_str(), password.c_str());
        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, m_connPool);
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "SELECT id FROM users WHERE username='%s' AND password='%s'",
                 username.c_str(), password.c_str());
        if (mysql_query(mysql, sql) || !mysql_store_result(mysql)->row_count) {
            m_content_length = snprintf(m_content, sizeof(m_content),
                "{ \"status\": \"error\", \"msg\": \"登录失败\" }");
        } else {
            m_content_length = snprintf(m_content, sizeof(m_content),
                "{ \"status\": \"ok\", \"msg\": \"登录成功\" }");
        }
        return GET_REQUEST;
    }
    
    if (m_method == GET && strncmp(m_url, "/api/user/", 10) == 0) {
        int uid = atoi(m_url + 10);
        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, m_connPool);
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "SELECT id,username,email FROM users WHERE id=%d", uid);
        mysql_query(mysql, sql);
        MYSQL_RES* res = mysql_store_result(mysql);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            m_content_length = snprintf(m_content, sizeof(m_content),
                "{ \"id\": %s, \"username\": \"%s\", \"email\": \"%s\" }",
                row[0], row[1], row[2]);
        } else {
            m_content_length = snprintf(m_content, sizeof(m_content),
                "{ \"status\": \"error\", \"msg\": \"用户不存在\" }");
        }
        mysql_free_result(res);
        return GET_REQUEST;
    }
    LOG_DEBUG("sockfd:%d, m_method: %d", m_sockfd, m_method);
    LOG_DEBUG("sockfd:%d, m_url: %s", m_sockfd, m_url);
    LOG_DEBUG("sockfd:%d, m_doc_root: %s", m_sockfd, m_doc_root);
    LOG_DEBUG("sockfd:%d, m_address.sin_addr: %s", m_sockfd, inet_ntoa(m_address.sin_addr));
    LOG_DEBUG("sockfd:%d, m_content_length: %ld", m_sockfd, m_content_length);
    
    //开始走文件传输逻辑
    // 拼接真实路径
    strcpy(m_real_file, m_doc_root);
    int len = strlen(m_doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);// 确保不越界的路径拼接

    // 检查文件是否存在
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    if (fd < 0) return INTERNAL_ERROR;
    // 创建一个新的内存区域，并把一个文件（或设备）的全部或一部分数据和这个内存区域关联起来
    // 第一个参数传0表示让系统自动选择映射地址
    // PROT_READ 表示只读映射
    // MAP_PRIVATE 表示私有映射，修改不会写回原文件
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m_file_address == MAP_FAILED) {
        m_file_address = nullptr;
        return INTERNAL_ERROR;
    }
    LOG_DEBUG("sockfd:%d,Mapped file %s to memory at address %p", m_sockfd,m_real_file, m_file_address);
    return FILE_REQUEST;
}

// 解除映射文件
void http_conn::unmap_file()
{
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
    memset(&m_file_stat, 0, sizeof(m_file_stat));
}

bool http_conn::process_write(HTTP_CODE ret)
{
    LOG_DEBUG("sockfd:%d, Processing write for HTTP_CODE: %d", m_sockfd,ret);

    switch (ret) {
    case INTERNAL_ERROR:
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        add_response("%s", error_500_form);
        break;
    case BAD_REQUEST:
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        add_response("%s", error_400_form);
        break;
    case NO_RESOURCE:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        add_response("%s", error_404_form);
        break;
    case FORBIDDEN_REQUEST:
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        add_response("%s", error_403_form);
        break;
    // 对应文件请求
    case FILE_REQUEST: {
        // 1. 设置 HTTP 响应状态行
        // add_status_line()：添加 HTTP 响应的第一行，例如 "HTTP/1.1 200 OK"。
        add_status_line(200, ok_200_title);
        const char* ctype = "text/html; charset=utf-8";
        const char* dot = strrchr(m_real_file, '.');
        if (dot) {
            // 根据不同的文件后缀，设置不同的 Content-Type。
            if (!strcasecmp(dot, ".css")) ctype = "text/css";
            else if (!strcasecmp(dot, ".js")) ctype = "application/javascript";
            else if (!strcasecmp(dot, ".png")) ctype = "image/png";
            else if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) ctype = "image/jpeg";
            else if (!strcasecmp(dot, ".gif")) ctype = "image/gif";
            else if (!strcasecmp(dot, ".svg")) ctype = "image/svg+xml";
            else if (!strcasecmp(dot, ".txt")) ctype = "text/plain; charset=utf-8";
            else if (!strcasecmp(dot, ".pdf")) ctype = "application/pdf";
            else if (!strcasecmp(dot, ".mp4")) ctype = "video/mp4";
        }
        add_content_type(ctype);
        
        // 3. 添加其他 HTTP 响应头
        add_content_length(m_file_stat.st_size);
        add_linger();
        add_blank_line();
        
        // 第一个内存块：存放 HTTP 响应头
        m_iv[0].iov_base = m_write_buf; // 指向存放 HTTP 头的缓冲区。
        m_iv[0].iov_len  = m_write_idx; // 头部数据的长度。
        m_iv_count = 1; // 目前要发送的内存块数量是 1 (只有 HTTP 头)。

        // 5. 如果文件有效，准备发送文件内容
        if (m_file_stat.st_size > 0 && m_file_address) {
            // 第二个内存块：存放 mmap 映射的文件内容
            m_iv[1].iov_base = m_file_address; // 指向 mmap 映射的内存地址。
            m_iv[1].iov_len  = m_file_stat.st_size; // 文件内容的大小。
            m_iv_count = 2; // 现在要发送的内存块数量是 2 (HTTP 头 + 文件内容)。
        }

        // 6. 计算总共需要发送的字节数
        // m_bytes_to_send = 响应头的长度 + 文件的长度（如果有的话）。
        m_bytes_to_send = m_write_idx + (m_iv_count == 2 ? m_file_stat.st_size : 0);
        LOG_DEBUG("sockfd:%d,Prepared response for FILE_REQUEST, bytes to send: %zu",m_sockfd ,m_bytes_to_send);
        return true; // 表示准备工作完成，可以开始发送数据了。
    }
    // 得到了请求且不是文件请求
    case GET_REQUEST: {
        std::string body(m_content); // 如果 do_request 写了 JSON，直接用
        add_status_line(200, ok_200_title);
        add_content_type("application/json; charset=utf-8");
        add_content_length((int)body.size());
        add_linger();
        add_blank_line();
        add_response("%s", body.c_str());

        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len  = m_write_idx;
        m_iv_count = 1;
        m_bytes_to_send = m_write_idx;
        LOG_DEBUG("sockfd:%d,Prepared response for GET_REQUEST, bytes to send: %zu",m_sockfd ,m_bytes_to_send);
        return true;
    }
    default:
        return false;
    }

    // 错误页路径
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len  = m_write_idx;
    m_iv_count = 1;
    m_bytes_to_send = m_write_idx;
    return true;
}

void http_conn::add_response(const char *fmt, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE) return;
    va_list arg_list;
    va_start(arg_list, fmt);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, fmt, arg_list);
    va_end(arg_list);
    if (len >= 0) m_write_idx += len;
}

void http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

void http_conn::add_content_type(const char* type)
{
    add_response("Content-Type: %s\r\n", type);
}

void http_conn::add_status_line(int status, const char* title)
{
    add_response("HTTP/1.1 %d %s\r\n", status, title);
}

void http_conn::add_content_length(int content_len)
{
    add_response("Content-Length: %d\r\n", content_len);
}

void http_conn::add_linger()
{
    add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}

void http_conn::add_blank_line()
{
    add_response("\r\n");
}
