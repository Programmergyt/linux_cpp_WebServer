#ifndef TOOLS_H
#define TOOLS_H

#include <string>
#include <netinet/in.h>
#include "../timer/Timer.h" // 假设路径
#include <functional>     // for std::function
#include <unordered_map>
#include <algorithm>

struct util_timer;
class TimerManager;

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

    /**
     * @brief 设置文件描述符为非阻塞
     * @param fd 需要设置的文件描述符
     * @return 成功返回 0，失败返回 -1
     */
    static int setnonblocking(int fd);

    /**
     * @brief 向 epoll 实例中注册文件描述符
     * @param epollfd epoll 实例的文件描述符
     * @param fd 需要注册的文件描述符
     * @param one_shot 是否启用 EPOLLONESHOT 模式
     * @param TRIGMode 触发模式 (0 = LT, 1 = ET)
     */
    static void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    /**
     * @brief 信号处理函数
     * @param sig 信号编号
     */
    static void sig_handler(int sig);

    /** 
     * @brief 注册信号处理函数
     * @param sig 信号编号
     * @param handler 信号处理函数
     * @param restart 是否启用 SA_RESTART 自动重启被中断的系统调用
     */
    static void addsig(int sig, void(handler)(int), bool restart = true);

    // 根据给定路径递归创建父目录
    static void create_parent_dirs(const char *path);

    //从内核时间表删除描述符
    static void removefd(int epollfd, int fd);

    //修改在 epoll 实例中注册的文件描述符 fd 的事件,本质是在读事件和写事件之间进行切换
    static void modfd(int epollfd, int fd, int ev, int TRIGMode);

    // 获取文件的MIME类型
    static std::string get_mime_type(const std::string& file_path);
    
    // 解析表单数据的辅助函数
    static std::string parse_form_field(const std::string& body, const std::string& key);

    /**
     * @brief 初始化客户端数据并创建新定时器
     * @param tm 定时器管理器
     * @param cd 指向要初始化的 client_data
     * @param sockfd 客户端文件描述符
     * @param client_addr 客户端地址
     * @param timeout_sec 超时秒数
     * @param callback 超时回调函数
     */
    static void init_timer(TimerManager &tm, client_data *cd, int sockfd, const sockaddr_in &client_addr, int timeout_sec, std::function<void(client_data *)> callback);

    /**
     * @brief 调整（延长）一个现有定时器的超时时间
     * @param tm 定时器管理器
     * @param cd 指向关联的客户端数据 (cd->timer 必须有效)
     * @param timeout_sec 新的超时秒数
     */
    static void adjust_timer(TimerManager &tm, client_data *cd, int timeout_sec);

    /**
     * @brief 从管理器中删除一个定时器（用于非超时的主动关闭）
     * @param tm 定时器管理器
     * @param cd 指向关联的客户端数据 (cd->timer 必须有效)
     */
    static void del_timer(TimerManager &tm, client_data *cd);

    static int *u_pipefd;   // 管道，用于信号通知,保存socket两端的两个文件描述符fd
};

#endif // TOOLS_H
