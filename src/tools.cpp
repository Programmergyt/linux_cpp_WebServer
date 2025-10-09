#include "tools/tools.h"
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <sys/stat.h>
#include <unordered_map>
#include <algorithm>
#include <mutex>

// 用于保护epoll操作的全局锁
static std::mutex epoll_mutex;

int Tools::setnonblocking(int fd)
{
    // 获取原有属性
    int old_option = fcntl(fd, F_GETFL);
    // 添加非阻塞标志
    int new_option = old_option | O_NONBLOCK;
    // 应用设置
    fcntl(fd, F_SETFL, new_option);
    return old_option; // 返回旧属性，便于恢复
}

void Tools::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    // 选择触发模式（ET/LT）
    if (TRIGMode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    // 是否启用 EPOLLONESHOT
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }

    // 线程安全的epoll操作
    {
        std::lock_guard<std::mutex> lock(epoll_mutex);
        epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    }

    // 设置非阻塞
    setnonblocking(fd);
}

void Tools::sig_handler(int sig)
{
    int saved = errno;
    int msg = sig;
    // 用 write，且写 sizeof(int)
    ssize_t n = write(u_pipefd[1], &msg, sizeof(msg));
    (void)n; // 忽略返回值，这里只负责唤醒
    errno = saved;
}

void Tools::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART; // 自动重启系统调用
    sigfillset(&sa.sa_mask);       // 屏蔽所有信号，防止嵌套

    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction error");
        exit(EXIT_FAILURE);
    }
}

void Tools::show_error(int connfd, const char *info)
{
    // 向客户端发送错误消息
    send(connfd, info, strlen(info), 0);
    // 关闭连接
    close(connfd);
}

void Tools::create_parent_dirs(const char *path)
{
    std::string spath(path);
    size_t pos = 0;
    // 遍历路径中的每一级目录，逐步创建
    while ((pos = spath.find('/', pos + 1)) != std::string::npos)
    {
        std::string dir = spath.substr(0, pos);
        if (!dir.empty())
        {
            mkdir(dir.c_str(), 0755); // 若存在则返回错误，不影响
        }
    }
}

void Tools::timer_cb_func(client_data *user_data)
{
    epoll_ctl(Tools::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    if (user_data == nullptr) return;
    close(user_data->sockfd);
    std::cout << "[cb_func] closed sockfd=" << user_data->sockfd << "\n";
}

void Tools::removefd(int epollfd, int fd)
{

    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void Tools::modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

std::string Tools::get_mime_type(const std::string& file_path) {
    static const std::unordered_map<std::string, std::string> mime_types = {
        // 文本文件
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".txt", "text/plain"},
        {".xml", "text/xml"},
        {".csv", "text/csv"},
        
        // 图片文件
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".png", "image/png"},
        {".gif", "image/gif"},
        {".bmp", "image/bmp"},
        {".webp", "image/webp"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        
        // 文档文件
        {".pdf", "application/pdf"},
        {".doc", "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls", "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt", "application/vnd.ms-powerpoint"},
        {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        
        // 音视频文件
        {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"},
        {".mp4", "video/mp4"},
        {".avi", "video/x-msvideo"},
        
        // 压缩文件
        {".zip", "application/zip"},
        {".tar", "application/x-tar"},
        {".gz", "application/gzip"},
        {".rar", "application/x-rar-compressed"},
        
        // 字体文件
        {".ttf", "font/ttf"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        
        // 其他
        {".bin", "application/octet-stream"}
    };
    
    // 获取文件扩展名
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "application/octet-stream"; // 默认二进制类型
    }
    
    std::string extension = file_path.substr(dot_pos);
    // 转换为小写
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    auto it = mime_types.find(extension);
    if (it != mime_types.end()) {
        return it->second;
    }
    
    return "application/octet-stream"; // 默认二进制类型
}

std::string Tools::parse_form_field(const std::string& body, const std::string& key) {
    std::string search_key = key + "=";
    size_t pos = body.find(search_key);
    if (pos == std::string::npos) {
        return std::string();
    }
    
    size_t start = pos + search_key.length();
    size_t end = body.find("&", start);
    if (end == std::string::npos) {
        end = body.length();
    }
    
    return body.substr(start, end - start);
}

int *Tools::u_pipefd = nullptr;
int Tools::u_epollfd = 0;