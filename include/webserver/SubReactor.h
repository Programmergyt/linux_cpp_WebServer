#ifndef SUB_REACTOR_H
#define SUB_REACTOR_H

#include <sys/epoll.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include <unistd.h> // for pipe, close
#include <netinet/in.h> // for sockaddr_in

#include "../thread_pool/thread_pool.h"
#include "../http/HttpConnection.h"
#include "../http/ConnectionPool.h"
#include "../log/log.h"
#include "../timer/timer.h"
#include "../sql/sql_connection_pool.h"
#include "../handler/handler.h"
#include "../tools/tools.h" // 包含 Tools

const int SUB_MAX_EVENT_NUMBER = 10000;

class SubReactor
{
public:
    SubReactor();
    ~SubReactor();

    /**
     * @brief 初始化从 Reactor
     * @param pool 共享的工作线程池
     * @param connPool 共享的数据库连接池
     * @param router 共享的路由
     * @param context 共享的请求上下文
     * @param timeout_sec 超时时间
     * @param stop_flag 共享的服务器停止标志
     */
    void init(thread_pool *pool, connection_pool *connPool, Router *router, 
              RequestContext *context, int timeout_sec, std::atomic<bool> *stop_flag);

    /**
     * @brief 从 Reactor 的事件循环，在单独的线程中运行
     */
    void eventLoop();

    /**
     * @brief 获取通信管道的写入端 fd
     * @return 主 Reactor 应用此 fd 来发送消息
     */
    int getPipeFd() const;

private:
    /**
     * @brief 处理来自 HttpConnection 的动作（读、写、关闭）
     */
    void handle_action(int connfd, HttpConnection::Action action);

    /**
     * @brief 处理主 Reactor 分配的新连接
     * @param connfd 新的连接套接字
     */
    void handle_new_connection(int connfd);

    int m_epollfd;
    int m_pipefd[2]; // 用于与主 Reactor 通信 [0]=读, [1]=写
    epoll_event events[SUB_MAX_EVENT_NUMBER];

    // 本 Reactor 负责的连接
    std::vector<std::shared_ptr<ManagedConnection>> m_connections;
    std::mutex m_connections_mutex;

    // 定时器相关
    timer_manager m_timer_manager;
    std::vector<client_data> m_client_data;
    int m_timeout_sec;

    // 共享资源（来自 WebServer）
    thread_pool *m_pool;
    connection_pool *m_connPool;
    Router *m_router;
    RequestContext *m_context;
    std::atomic<bool> *m_stop_server;
};

#endif // SUB_REACTOR_H