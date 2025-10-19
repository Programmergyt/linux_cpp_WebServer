#include "webserver/SubReactor.h"
#include <string.h> // for bzero

int MAX_FD = 65536;

SubReactor::SubReactor()
    : m_epollfd(-1), m_timeout_sec(15), m_pool(nullptr),
      m_connPool(nullptr), m_router(nullptr), m_context(nullptr),
      m_stop_server(nullptr)
{
    m_pipefd[0] = -1;
    m_pipefd[1] = -1;
    // 调整容器大小以通过 fd 直接索引
    m_connections.resize(MAX_FD + 1);
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

    // 清理所有剩余的连接和定时器
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        m_connections.clear();
    }
    
    for (auto& client : m_client_data) {
        if (client.timer && !client.timer_deleted) {
            client.timer_deleted = true;
            m_timer_manager.del_timer(client.timer);
            client.timer = nullptr;
        }
    }
}

void SubReactor::init(thread_pool *pool, connection_pool *connPool, Router *router,
                      RequestContext *context, int timeout_sec, std::atomic<bool> *stop_flag)
{
    m_pool = pool;
    m_connPool = connPool;
    m_router = router;
    m_context = context;
    m_timeout_sec = timeout_sec;
    m_stop_server = stop_flag;

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
}

int SubReactor::getPipeFd() const
{
    // 返回管道的写端，供主 Reactor 写入
    return m_pipefd[1];
}

void SubReactor::handle_action(int connfd, HttpConnection::Action action)
{
    // 这个函数与旧 WebServer 中的实现完全相同
    switch (action)
    {
    case HttpConnection::Action::Read:
        Tools::modfd(m_epollfd, connfd, EPOLLIN, 1);
        break;
    case HttpConnection::Action::Wait:
        Tools::modfd(m_epollfd, connfd, EPOLLIN, 1);
        break;
    case HttpConnection::Action::Write:
        Tools::modfd(m_epollfd, connfd, EPOLLOUT, 1);
        break;
    case HttpConnection::Action::Close:
        // 从 epoll 删除并关闭 fd
        Tools::del_timer(m_timer_manager, &m_client_data[connfd]);
        {
            std::lock_guard<std::mutex> lock(m_connections_mutex);
            if (m_connections[connfd])m_connections[connfd].reset();
        }
        Tools::removefd(m_epollfd, connfd); // 确保 fd 被关闭
        LOG_DEBUG("SubReactor: Connection closed by action: fd=%d", connfd);
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
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        m_connections[connfd] = std::make_shared<ManagedConnection>(connfd, client_addr, m_router, m_context);
    }

    // 3. 创建定时器回调函数
    std::function<void(client_data *)> timeout_cb = [this](client_data *user_data)
    {
        if (user_data && user_data->sockfd >= 0 && !user_data->timer_deleted)
        {
            user_data->timer_deleted = true; // 标记定时器已被删除
            {
                std::lock_guard<std::mutex> lock(m_connections_mutex);
                if (m_connections[user_data->sockfd])m_connections[user_data->sockfd].reset();
            }
            Tools::removefd(m_epollfd, user_data->sockfd); // 从 epoll 删除并 close(fd)
            LOG_DEBUG("SubReactor: Connection timeout and closed: fd=%d", user_data->sockfd);
            user_data->timer = nullptr; // 清空指针
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
                        LOG_DEBUG("SubReactor: Timer tick executed");
                    }
                }
                if (*m_stop_server) break; // 跳出外层 for 循环
            }
            // 2. 处理连接错误
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                LOG_DEBUG("SubReactor: Client disconnected (EPOLLERR/HUP): %d", sockfd);
                // 删除定时器
                Tools::del_timer(m_timer_manager, &m_client_data[sockfd]);
                // 清理连接
                {
                    std::lock_guard<std::mutex> lock(m_connections_mutex);
                    if (m_connections[sockfd])m_connections[sockfd].reset();
                }
                // 从 epoll 移除并关闭
                Tools::removefd(m_epollfd, sockfd);
            }
            // 3. 处理读事件 (EPOLLIN)
            else if (events[i].events & EPOLLIN)
            {
                LOG_DEBUG("SubReactor: EPOLLIN event on fd %d", sockfd);
                std::shared_ptr<ManagedConnection> conn_shared;
                {
                    std::lock_guard<std::mutex> lock(m_connections_mutex);
                    if (m_connections[sockfd]) {
                        conn_shared = m_connections[sockfd]; // 拷贝，引用计数+1
                    }
                }
                if (conn_shared) {
                    // 更新定时器
                    Tools::adjust_timer(m_timer_manager, &m_client_data[sockfd], m_timeout_sec);
                    // 派发任务到工作线程池
                    m_pool->append([conn_shared, this, sockfd]() {
                        HttpConnection::Action action = conn_shared->get()->handle_read();
                        handle_action(sockfd, action);
                    });
                }
            }
            // 4. 处理写事件 (EPOLLOUT)
            else if (events[i].events & EPOLLOUT)
            {
                LOG_DEBUG("SubReactor: EPOLLOUT event on fd %d", sockfd);
                std::shared_ptr<ManagedConnection> conn_shared;
                {
                    std::lock_guard<std::mutex> lock(m_connections_mutex);
                    if (m_connections[sockfd]) {
                        conn_shared = m_connections[sockfd]; // 拷贝
                    }
                }
                if (conn_shared) {
                    // 更新定时器
                    Tools::adjust_timer(m_timer_manager, &m_client_data[sockfd], m_timeout_sec);
                    // 派发任务到工作线程池
                    m_pool->append([conn_shared, this, sockfd]() {
                        HttpConnection::Action action = conn_shared->get()->handle_write();
                        handle_action(sockfd, action);
                    });
                }
            }
        } // end for
    } // end while

    LOG_INFO("SubReactor shutting down...");
}