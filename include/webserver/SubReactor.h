#ifndef SUB_REACTOR_H
#define SUB_REACTOR_H

#include <sys/epoll.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include <queue>          // for std::queue
#include <sys/eventfd.h>  // for eventfd
#include <unistd.h> // for pipe, close
#include <netinet/in.h> // for sockaddr_in

#include "../thread_pool/ThreadPool.h"
#include "../http/HttpConnection.h"
#include "../http/HttpConnectionPool.h"
#include "../log/Log.h"
#include "../timer/Timer.h"
#include "../sql/SqlConnectionPool.h"
#include "../handler/Handler.h"
#include "../tools/Tools.h" // 包含 Tools
#include "../websocket/WebSocketServer.h" // 新增：包含 WebSocketServer
#include "../websocket/WebSocketConn.h"   // 新增：包含 WebSocketConn

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
    void init(ThreadPool *pool, SqlConnectionPool *connPool, Router *router, 
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

    // 用于工作线程向此 Reactor 提交任务
    void addTask(std::function<void()> task);

private:
    /**
     * @brief 处理来自 HttpConnection 的动作（读、写、关闭）
     */
    void handle_action(int connfd, Action action);

    /**
     * @brief 处理主 Reactor 分配的新连接
     * @param connfd 新的连接套接字
     */
    void handle_new_connection(int connfd);

    /**
     * @brief 处理工作线程提交的唤醒事件
     */
    void handle_wakeup();

    int m_epollfd;
    int m_pipefd[2]; // 用于与主 Reactor 通信 [0]=读, [1]=写
    epoll_event events[SUB_MAX_EVENT_NUMBER];

    // 本 Reactor 负责的连接
    std::vector<std::shared_ptr<ManagedConnection>> m_connections;
    
    // WebSocket 连接管理（本地存储，但使用全局单例的 WebSocketServer）
    std::vector<std::shared_ptr<WebSocketConn>> m_ws_connections;

    // 定时器相关
    TimerManager m_timer_manager;
    std::vector<client_data> m_client_data;
    int m_timeout_sec;

    // 共享资源（来自 WebServer）
    ThreadPool *m_pool;
    SqlConnectionPool *m_connPool;
    Router *m_router;
    RequestContext *m_context;
    std::atomic<bool> *m_stop_server;

    // 用于本Reactor的线程间通信的成员
    int m_wakeup_fd;                     // 用于唤醒 epoll_wait 的 eventfd
    std::queue<std::function<void()>> m_task_queue; // 任务队列
    std::mutex m_task_mutex;             // 保护任务队列的互斥锁
};

#endif // SUB_REACTOR_H