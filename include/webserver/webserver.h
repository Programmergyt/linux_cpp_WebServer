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

#include "../thread_pool/thread_pool.h"
#include "../http/HttpConnection.h"
#include "../log/log.h"
#include "../timer/timer.h"
#include "../sql/sql_connection_pool.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数

class WebServer
{
public:
    WebServer();
    ~WebServer();
    void init(int port, string databaseURL, string user, string passWord, string databaseName,
              int opt_linger, int sql_num,
              int thread_num, int close_log);
    void eventListen();
    void eventLoop();

public:
    // 基础配置
    int m_port;
    char *m_root;
    int m_close_log;
    int m_epollfd;
    int m_listenfd;
    epoll_event events[MAX_EVENT_NUMBER];
    std::vector<std::unique_ptr<HttpConnection>> m_connections;
    int m_pipefd[2];
    bool stop_server;
    Router m_router;
    RequestContext m_context;

    // 数据库相关
    connection_pool *m_connPool;
    string m_databaseURL;  // 数据库地址
    string m_user;         // 登陆数据库用户名
    string m_passWord;     // 登陆数据库密码
    string m_databaseName; // 使用数据库名
    int m_sql_num;

    // 线程池相关
    thread_pool *m_pool;
    int m_thread_num;
    void handle_action(int connfd, HttpConnection::Action action);
};
#endif