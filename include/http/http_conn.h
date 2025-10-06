// #ifndef HTTP_CONN_H
// #define HTTP_CONN_H
// #include <unistd.h>
// #include <signal.h>
// #include <sys/types.h>
// #include <sys/epoll.h>
// #include <fcntl.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <assert.h>
// #include <sys/stat.h>
// #include <string.h>
// #include <pthread.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <sys/mman.h>
// #include <stdarg.h>
// #include <errno.h>
// #include <sys/wait.h>
// #include <sys/uio.h>
// #include <map>
// #include <string>

// #include "../sql/sql_connection_pool.h"
// #include "../log/log.h"
// #include "../tools/tools.h"

// class http_conn
// {
// public:
//     static const int FILENAME_LEN = 200;       // 文件名最大长度
//     static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区大小
//     static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区大小
//     static int m_epoll_fd;                     // proactor模型下的 epoll fd

//     enum METHOD
//     {
//         GET = 0,
//         POST,
//         HEAD,
//         PUT,
//         DELETE_,
//         TRACE,
//         OPTIONS,
//         CONNECT_,
//         PATCH_
//     };
//     enum CHECK_STATE
//     {
//         CHECK_STATE_REQUESTLINE = 0,
//         CHECK_STATE_HEADER,
//         CHECK_STATE_CONTENT
//     };
//     enum HTTP_CODE
//     {
//         NO_REQUEST,
//         GET_REQUEST,
//         BAD_REQUEST,
//         NO_RESOURCE,
//         FORBIDDEN_REQUEST,
//         FILE_REQUEST,
//         INTERNAL_ERROR,
//         CLOSED_CONNECTION,
//         REDIRECT_REQUEST
//     };
//     enum LINE_STATUS
//     {
//         LINE_OK = 0,
//         LINE_BAD,
//         LINE_OPEN
//     };

//     // Reactor/Proactor 的一次“任务”要处理的 IO 意图
//     enum IO_STATE
//     {
//         IO_NONE = 0,
//         IO_READ,
//         IO_WRITE
//     };

// public:
//     http_conn();
//     ~http_conn();

//     // 固定使用ET模式
//     void init(int sockfd, const sockaddr_in &addr, char *root, int close_log);

//     void set_connection_pool(connection_pool *connPool) { m_connPool = connPool; }

//     // 由上层在 Reactor 下赋值：本次工作线程处理读还是写
//     void set_io_state(IO_STATE s) { m_io_state = s; }

//     // 新增：返回待发送字节数的接口
//     size_t bytes_to_send() { return m_bytes_to_send; }

//     void close_conn(bool real_close = true);

//     // 从 socket 读数据（主线程或工作线程调用，取决于模型）
//     bool read_once();

//     // 向 socket 写数据（主线程或工作线程调用，取决于模型）
//     bool write_once();

//     // 处理一次 HTTP 请求（解析 + 业务 + 组织响应）；返回后可视情况继续 EPOLLOUT
//     void process();

//     // 对外暴露：是否保持连接
//     bool keep_alive() const { return m_linger; }

// private:
//     // 只重置解析/缓冲状态，不动 sockfd 等
//     void init();
//     // 解析 HTTP 请求
//     HTTP_CODE process_read();
//     // 组织 HTTP 响应
//     bool process_write(HTTP_CODE ret);
//     // 业务处理（映射静态文件；演示 POST 回显）
//     HTTP_CODE do_request();

//     LINE_STATUS parse_line();
//     HTTP_CODE parse_request_line(char *text);
//     HTTP_CODE parse_headers(char *text);
//     HTTP_CODE parse_content(char *text);

//     char *get_line() { return m_read_buf + m_start_line; }

//     void unmap_file();

//     // 写响应的若干工具
//     void add_response(const char *fmt, ...);
//     void add_headers(int content_len);
//     void add_content_type(const char *type);
//     void add_status_line(int status, const char *title);
//     void add_content_length(int content_len);
//     void add_linger();
//     void add_blank_line();

// private:
//     // 基础连接信息
//     int m_sockfd;

//     sockaddr_in m_address;

//     // 日志开关 / 模型 (固定使用ET模式)
//     int m_close_log;           // 0=开日志, 1=关日志
//     IO_STATE m_io_state;       // Reactor 下：本次任务处理读或写
//     CHECK_STATE m_check_state; // 解析到哪部分

//     // 读缓冲
//     char m_read_buf[READ_BUFFER_SIZE];
//     int m_read_idx;    // 已读入缓冲的最后一个字节的下一个位置
//     int m_checked_idx; // 当前正在解析的字符位置
//     int m_start_line;  // 当前正在解析的行的起始位置
//     int m_body_index;  // body 数据在读缓冲区的起始索引
//     // 写缓冲
//     char m_write_buf[WRITE_BUFFER_SIZE];
//     int m_write_idx; // 写缓冲区中待发送的字节数

//     // 解析出来的信息
//     METHOD m_method;
//     char m_url[FILENAME_LEN];
//     char *m_version;
//     char *m_host;
//     long m_content_length;
//     char m_content_type[100];
//     bool m_linger;                    // keep-alive
//     char m_content[READ_BUFFER_SIZE]; // body (示例用)

//     // 资源路径
//     char *m_doc_root;
//     char m_real_file[FILENAME_LEN];

//     // 映射的文件
//     char *m_file_address;
//     struct stat m_file_stat; // 文件状态

//     // 分散写
//     // m_iv[0]: 第一个iovec结构体，它的 iov_base 指向HTTP头部的缓冲区 m_write_buf，iov_len 是头部数据的长度。
//     // m_iv[1]: 第二个iovec结构体，它的 iov_base 指向通过mmap映射的文件内存地址 m_file_address，iov_len 是文件的大小。
//     struct iovec m_iv[2];
//     int m_iv_count; // iovec结构体的数量，如果有文件要发送，则为2，否则为1。
//     size_t m_bytes_to_send;
//     size_t m_bytes_have_sent;

//     // 数据库（可选使用）
//     connection_pool *m_connPool; // 不在此处使用，保留扩展
// };

// #endif