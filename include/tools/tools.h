#ifndef TOOLS_H
#define TOOLS_H

#include <string>
#include <netinet/in.h>
#include <unordered_map>
#include <algorithm>

class util_timer; // 前向声明，避免循环依赖

// 客户端信息结构体
struct client_data {
    sockaddr_in address; // 客户端地址
    int sockfd;          // 套接字
    util_timer* timer;   // 对应的定时器
    bool timer_deleted;  // 标记定时器是否已被删除
    
    client_data() : sockfd(-1), timer(nullptr), timer_deleted(false) {}
};

class Tools {
public:
    // 设置文件描述符为非阻塞
    static int setnonblocking(int fd);

    // 向 epoll 实例中注册文件描述符
    // epollfd: epoll 实例
    // fd: 待注册的文件描述符
    // one_shot: 是否启用 EPOLLONESHOT
    // TRIGMode: 触发模式 (0 = LT, 1 = ET)
    static void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 信号处理函数（接收到信号时调用）
    static void sig_handler(int sig);

    // 注册信号处理函数
    // sig: 信号编号
    // handler: 信号处理函数
    // restart: 是否启用 SA_RESTART 自动重启被中断的系统调用
    static void addsig(int sig, void(handler)(int), bool restart = true);

    // 向客户端显示错误信息并关闭连接
    static void show_error(int connfd, const char *info);

    // 根据给定路径递归创建父目录
    static void create_parent_dirs(const char *path);

    // 定时器回调函数
    static void timer_cb_func(client_data* user_data);

    //从内核时间表删除描述符
    static void removefd(int epollfd, int fd);

    //修改在 epoll 实例中注册的文件描述符 fd 的事件,本质是在读事件和写事件之间进行切换
    static void modfd(int epollfd, int fd, int ev, int TRIGMode);

    // 获取文件的MIME类型
    static std::string get_mime_type(const std::string& file_path);
    
    // 解析表单数据的辅助函数
    static std::string parse_form_field(const std::string& body, const std::string& key);

    static int *u_pipefd;   // 管道，用于信号通知,保存socket两端的两个文件描述符fd
    static int u_epollfd;     // 全局 epoll fd
};

#endif // TOOLS_H
