#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <vector>     // 新增
#include <thread>     // 新增
#include <atomic>     // m_stop_server 已是 atomic
#include <memory>     // 新增

#include "../thread_pool/ThreadPool.h"
#include "../log/Log.h"
#include "../timer/Timer.h"             
#include "../sql/SqlConnectionPool.h"
#include "../handler/Handler.h"
#include "SubReactor.h"                 // 包含从 Reactor

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数

class WebServer
{
public:
    WebServer();
    ~WebServer();
    /**
     * @brief 初始化 Web 服务器
     * @param port 监听端口
     * @param databaseURL 数据库连接 URL
     * @param user 数据库用户名
     * @param passWord 数据库密码
     * @param databaseName 数据库名称
     * @param sql_num 数据库连接池大小
     * @param thread_num 工作线程池线程数
     * @param close_log 日志开关
     * @param timeout_sec 连接超时时间（秒）
     */
    void init(int port, string databaseURL, string user, string passWord, string databaseName,
              int sql_num,int thread_num, int close_log, int timeout_sec = 3);
    /**
     * @brief 开始监听事件
     */
    void eventListen();
    /**
     * @brief 事件循环
     */
    void eventLoop();

public:
    // 基础配置
    int m_port;
    char *m_root;
    int m_close_log;
    int m_epollfd;    // 主 Reactor 的 epoll
    int m_listenfd;
    epoll_event events[MAX_EVENT_NUMBER];
    int m_pipefd[2];  // 主 Reactor 的信号管道
    std::atomic<bool> stop_server;
    Router m_router;
    RequestContext m_context;
    int m_timeout_sec; // 超时时间（秒）

    // 从 Reactor 相关
    int m_sub_reactor_num; // 从 Reactor 的数量
    std::vector<SubReactor*> m_sub_reactors; // 从 Reactor 实例
    std::vector<std::thread> m_sub_threads; // 从 Reactor 运行的线程
    int m_round_robin_counter; // 用于轮询分发连接

    // 数据库相关 (共享资源)
    SqlConnectionPool *m_connPool;
    string m_databaseURL;
    string m_user;
    string m_passWord;
    string m_databaseName;
    int m_sql_num;

    // 线程池相关 (与SubReactor共享的"工作"线程池)
    ThreadPool *m_pool;
    int m_thread_num; // 工作线程池的线程数

};
#endif // WEBSERVER_H