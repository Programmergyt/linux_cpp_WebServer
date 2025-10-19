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

#include "../thread_pool/thread_pool.h"
#include "../http/HttpConnection.h"     // 仍需包含
#include "../http/ConnectionPool.h"     // 仍需包含
#include "../log/log.h"
#include "../timer/timer.h"             // 仍需包含
#include "../sql/sql_connection_pool.h"
#include "../handler/handler.h"
#include "SubReactor.h"                 // 新增: 包含从 Reactor

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数

class WebServer
{
public:
    WebServer();
    ~WebServer();
    void init(int port, string databaseURL, string user, string passWord, string databaseName,
              int sql_num,int thread_num, int close_log, int timeout_sec = 3);
    void eventListen();
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

    // --- 移除的成员 ---
    // std::vector<std::shared_ptr<ManagedConnection>> m_connections;
    // std::mutex m_connections_mutex;
    // timer_manager m_timer_manager;
    // std::vector<client_data> m_client_data;

    // --- 新增的成员 ---
    int m_sub_reactor_num; // 从 Reactor 的数量
    std::vector<SubReactor*> m_sub_reactors; // 从 Reactor 实例
    std::vector<std::thread> m_sub_threads; // 从 Reactor 运行的线程
    int m_round_robin_counter; // 用于轮询分发连接

    // 数据库相关 (共享资源)
    connection_pool *m_connPool;
    string m_databaseURL;
    string m_user;
    string m_passWord;
    string m_databaseName;
    int m_sql_num;

    // 线程池相关 (共享的"工作"线程池)
    thread_pool *m_pool;
    int m_thread_num; // 工作线程池的线程数

    // --- 移除的成员 ---
    // void handle_action(int connfd, HttpConnection::Action action);
};
#endif