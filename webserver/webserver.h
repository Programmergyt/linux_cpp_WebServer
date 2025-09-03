#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "../thread_pool/thread_pool.h"
#include "../http/http_conn.h"
#include "../log/log.h"
#include "../timer/timer.h"
#include "../sql/sql_connection_pool.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5000;            //最小超时单位

class WebServer 
{
public:
    WebServer();
    ~WebServer();
    void init(int port , string databaseURL,string user, string passWord, string databaseName,
              int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);
    void eventListen();
    void eventLoop();
public:
    // 基础配置
    int m_port;
    char *m_root;
    int m_close_log;
    int m_actormodel;
    int m_epollfd;
    int m_listenfd;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;
    epoll_event events[MAX_EVENT_NUMBER];
    http_conn *users;
    int m_pipefd[2];
    bool stop_server;

    // 数据库相关
    connection_pool *m_connPool;
    string m_databaseURL;      //数据库地址
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    thread_pool *m_pool;
    int m_thread_num;

    //定时器相关
    timer_manager m_timer_mgr;         // 定时器管理器
    client_data users_timer[MAX_FD];   // 每个连接对应的定时器数据
};
#endif