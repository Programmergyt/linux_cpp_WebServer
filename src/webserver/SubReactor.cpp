#include "webserver/SubReactor.h"
#include <string.h> // for bzero

int MAX_FD = 65536;

SubReactor::SubReactor()
    : m_epollfd(-1), m_timeout_sec(15), m_pool(nullptr),
      m_connPool(nullptr), m_router(nullptr), m_context(nullptr),
      m_stop_server(nullptr),m_wakeup_fd(-1) // 新增：初始化 m_wakeup_fd
{
    m_pipefd[0] = -1;
    m_pipefd[1] = -1;
    m_connections.resize(MAX_FD + 1);
    m_ws_connections.resize(MAX_FD + 1);
    m_client_data.resize(MAX_FD + 1);
}

SubReactor::~SubReactor()
{
    // 关闭 epoll 和管道
    if (m_epollfd != -1)
    {
        close(m_epollfd);
    }
    if (m_pipefd[0] != -1)
    {
        close(m_pipefd[0]);
    }
    if (m_pipefd[1] != -1)
    {
        close(m_pipefd[1]);
    }
    if (m_wakeup_fd != -1) 
    {
        close(m_wakeup_fd);
    }

    // 清理所有剩余的连接和定时器
    m_connections.clear();
    m_ws_connections.clear();
    
    for (auto& client : m_client_data) {
        if (client.timer && !client.timer_deleted) {
            client.timer_deleted = true;
            m_timer_manager.del_timer(client.timer);
            client.timer = nullptr;
        }
    }
}

void SubReactor::init(ThreadPool *pool, SqlConnectionPool *connPool, Router *router,
                      RequestContext *context, int timeout_sec, std::atomic<bool> *stop_flag)
{
    m_pool = pool;
    m_connPool = connPool;
    m_router = router;
    m_context = context;
    m_timeout_sec = timeout_sec;
    m_stop_server = stop_flag;

    // 设置 WebSocket 服务器上下文（单例模式）
    WebSocketServer::getInstance().setContext(context);

    // 1. 创建自己的 epoll
    m_epollfd = epoll_create(5);
    if (m_epollfd == -1)
    {
        perror("SubReactor epoll_create error");
        exit(EXIT_FAILURE);
    }

    // 2. 创建与主 Reactor 通信的管道
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd) == -1)
    {
        perror("SubReactor socketpair error");
        exit(EXIT_FAILURE);
    }
    
    // 3. 设置管道读端为非阻塞，并加入 epoll (LT 模式)
    Tools::setnonblocking(m_pipefd[0]);
    Tools::addfd(m_epollfd, m_pipefd[0], false, 0); // LT 模式

    // 4. 创建并注册 eventfd 用于线程间通信
    m_wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeup_fd < 0) {
        LOG_ERROR("Failed to create eventfd");
        std::cerr << "Failed to create eventfd" << std::endl;
        exit(EXIT_FAILURE);
    }
    Tools::addfd(m_epollfd, m_wakeup_fd, false, 0); // 使用 LT 模式监听
}

void SubReactor::addTask(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(m_task_mutex);
        m_task_queue.push(std::move(task));
    }
    
    // 唤醒 SubReactor 线程
    uint64_t one = 1;
    ssize_t n = write(m_wakeup_fd, &one, sizeof(one));
    if (n != sizeof(one)) {
        LOG_ERROR("write to m_wakeup_fd error");
    }
}

void SubReactor::handle_wakeup()
{
    uint64_t one;
    ssize_t n = read(m_wakeup_fd, &one, sizeof(one)); // 必须读，以清空 eventfd 计数器
    if (n != sizeof(one)) {
        LOG_ERROR("read from m_wakeup_fd error");
    }

    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_task_mutex);
        tasks.swap(m_task_queue); // 高效交换，减少锁的持有时间
    }

    while (!tasks.empty())
    {
        std::function<void()> task = tasks.front();
        tasks.pop();
        task(); // 在 SubReactor 线程中执行任务
    }
}

int SubReactor::getPipeFd() const
{
    // 返回管道的写端，供主 Reactor 写入
    return m_pipefd[1];
}

void SubReactor::handle_action(int connfd, Action action)
{
    // 判断是 WebSocket 还是 HTTP 连接
    bool is_ws = WebSocketServer::getInstance().is_websocket_conn(connfd);
    
    switch (action)
    {
    case Action::Read:
        Tools::modfd(m_epollfd, connfd, EPOLLIN, 1);
        break;
    case Action::Wait:
        Tools::modfd(m_epollfd, connfd, EPOLLIN, 1);
        break;
    case Action::Write:
        Tools::modfd(m_epollfd, connfd, EPOLLOUT, 1);
        break;
    case Action::Close:
        // 从 epoll 删除并关闭 fd
        if (is_ws) {
            // WebSocket 连接
            WebSocketServer::getInstance().removeConnection(connfd);
            if (m_ws_connections[connfd]) {
                m_ws_connections[connfd].reset();
            }
        } else {
            // HTTP 连接
            Tools::del_timer(m_timer_manager, &m_client_data[connfd]);
            if (m_connections[connfd]) {
                m_connections[connfd].reset();
            }
        }
        Tools::removefd(m_epollfd, connfd); // 确保 fd 被关闭
        LOG_DEBUG("SubReactor: Connection closed by action: fd=%d", connfd);
        break;
    case Action::Upgrade:
        // HTTP 协议升级为 WebSocket
        Tools::del_timer(m_timer_manager, &m_client_data[connfd]);
        if (m_connections[connfd]) {
            m_connections[connfd].reset();
        }
        // 创建 WebSocket 连接对象
        m_ws_connections[connfd] = std::make_shared<WebSocketConn>(connfd, &WebSocketServer::getInstance(), m_context);
        // 注册此 fd 的事件注册回调函数
        WebSocketServer::getInstance().registerCallback(connfd, [this](int fd, Action action) {
            this->addTask([this, fd, action]() {
                this->handle_action(fd, action);
            });
        });
        WebSocketServer::getInstance().addConnection(connfd, m_ws_connections[connfd]);
        Tools::modfd(m_epollfd, connfd, EPOLLIN, 1);
        LOG_DEBUG("SubReactor: Connection upgraded to WebSocket: fd=%d", connfd);
        break;
    default:
        LOG_ERROR("SubReactor: Unknown action for fd %d", connfd);
        break;
    }
}

void SubReactor::handle_new_connection(int connfd)
{
    // 1. 将新连接添加到本 Reactor 的 epoll (ET, ONESHOT)
    Tools::addfd(m_epollfd, connfd, true, 1);
    LOG_INFO("SubReactor: New client connected: %d", connfd);

    // 2. 创建 HTTP 连接对象
    // httpconnection 构造函数需要 sockaddr_in 参数，但实际上没有使用这个参数，所以传入一个空的 sockaddr_in
    sockaddr_in client_addr;
    bzero(&client_addr, sizeof(client_addr));
    m_connections[connfd] = std::make_shared<ManagedConnection>(connfd, client_addr, m_router, m_context);

    // 3. 创建定时器回调函数，回调函数在m_timer_manager.tick()时被调用，所以与连接关闭是同步的
    std::function<void(client_data *)> timeout_cb = [this](client_data *user_data)
    {
        if (user_data && user_data->sockfd >= 0 && !user_data->timer_deleted)
        {
            this->addTask([this, sockfd = user_data->sockfd]() {
                LOG_DEBUG("SubReactor: Connection timeout, scheduling close action for fd=%d", sockfd);
                if (m_connections[sockfd]) {
                    // 调用标准的关闭流程
                    handle_action(sockfd, Action::Close);
                }
            });
        }
    };

    // 4. 初始化并添加定时器
    Tools::init_timer(m_timer_manager, &m_client_data[connfd], connfd, client_addr, m_timeout_sec, timeout_cb);
}

void SubReactor::eventLoop()
{
    while (!*m_stop_server)
    {
        int num = epoll_wait(m_epollfd, events, SUB_MAX_EVENT_NUMBER, -1);
        if (num < 0 && errno != EINTR)
        {
            LOG_ERROR("SubReactor: epoll failure");
            break;
        }

        for (int i = 0; i < num; ++i)
        {
            int sockfd = events[i].data.fd;

            // 1. 处理来自主 Reactor 管道的消息
            if (sockfd == m_pipefd[0])
            {
                // 消息是 int 类型
                int msg_buf[8]; // 一次最多读8个消息
                int ret = read(m_pipefd[0], msg_buf, sizeof(msg_buf));
                
                if (ret <= 0) {
                    continue; // EAGAIN 或错误
                }
                
                int msg_count = ret / sizeof(int);
                for (int j = 0; j < msg_count; ++j)
                {
                    int msg = msg_buf[j];
                    if (msg >= 0)
                    {
                        // 新连接
                        handle_new_connection(msg);
                    }
                    else if (msg == -1)
                    {
                        // 停止信号
                        *m_stop_server = true;
                        LOG_INFO("SubReactor: Received STOP signal.");
                        break; // 跳出 for 循环，外层 while 会检测到 stop
                    }
                    else if (msg == -2)
                    {
                        // 定时器 Tick 信号
                        m_timer_manager.tick();
                        // LOG_DEBUG("SubReactor: Timer tick executed");
                    }
                }
                if (*m_stop_server) break; // 跳出外层 for 循环
            }
            // 2. 新增：处理来自工作线程的唤醒事件
            else if (sockfd == m_wakeup_fd)
            {
                handle_wakeup();
            }
            // 3. 处理连接错误
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                this->addTask([this, sockfd]() {
                    handle_action(sockfd, Action::Close);
                });
            }
            // 4. 处理读事件 (EPOLLIN)
            else if (events[i].events & EPOLLIN)
            {
                LOG_DEBUG("SubReactor: EPOLLIN event on fd %d", sockfd);
                
                // 判断是 WebSocket 还是 HTTP 连接
                if (WebSocketServer::getInstance().is_websocket_conn(sockfd)) {
                    // WebSocket 连接
                    std::shared_ptr<WebSocketConn> ws_conn;
                    {
                        if (m_ws_connections[sockfd]) {
                            ws_conn = m_ws_connections[sockfd];
                        }
                    }
                    if (ws_conn) {
                        // 派发任务到工作线程池
                        m_pool->append([ws_conn, this, sockfd]() {
                            Action action = ws_conn->handle_read();
                            this->addTask([this, sockfd, action]() {
                                handle_action(sockfd, action);
                            });
                        });
                    }
                } else {
                    // HTTP 连接
                    std::shared_ptr<ManagedConnection> conn_shared;
                    {
                        if (m_connections[sockfd]) {
                            conn_shared = m_connections[sockfd]; // 拷贝，引用计数+1
                        }
                    }
                    if (conn_shared) {
                        // 更新定时器
                        Tools::adjust_timer(m_timer_manager, &m_client_data[sockfd], m_timeout_sec);
                        // 派发任务到工作线程池
                        m_pool->append([conn_shared, this, sockfd]() {
                            Action action = conn_shared->get()->handle_read();
                            this->addTask([this, sockfd, action]() {
                                handle_action(sockfd, action);
                            });
                        });
                    }
                }
            }
            // 5. 处理写事件 (EPOLLOUT)
            else if (events[i].events & EPOLLOUT)
            {
                LOG_DEBUG("SubReactor: EPOLLOUT event on fd %d", sockfd);
                
                // 判断是 WebSocket 还是 HTTP 连接
                if (WebSocketServer::getInstance().is_websocket_conn(sockfd)) {
                    // WebSocket 连接
                    std::shared_ptr<WebSocketConn> ws_conn;
                    {
                        if (m_ws_connections[sockfd]) {
                            ws_conn = m_ws_connections[sockfd];
                        }
                    }
                    if (ws_conn) {
                        // 派发任务到工作线程池
                        m_pool->append([ws_conn, this, sockfd]() {
                            Action action = ws_conn->handle_write();
                            this->addTask([this, sockfd, action]() {
                                handle_action(sockfd, action);
                            });
                        });
                    }
                } else {
                    // HTTP 连接
                    std::shared_ptr<ManagedConnection> conn_shared;
                    {
                        if (m_connections[sockfd]) {
                            conn_shared = m_connections[sockfd]; // 拷贝
                        }
                    }
                    if (conn_shared) {
                        // 更新定时器
                        Tools::adjust_timer(m_timer_manager, &m_client_data[sockfd], m_timeout_sec);
                        // 派发任务到工作线程池
                        m_pool->append([conn_shared, this, sockfd]() {
                            Action action = conn_shared->get()->handle_write();
                            this->addTask([this, sockfd, action]() {
                                handle_action(sockfd, action);
                            });
                        });
                    }
                }
            }
        } 
    } 

    LOG_INFO("SubReactor shutting down...");
}