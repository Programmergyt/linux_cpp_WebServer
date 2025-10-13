#include "webserver/webserver.h"
#include "handler/handler.h"
#include "http/ConnectionPool.h"

void WebServer::handle_action(int connfd, HttpConnection::Action action)
{

    switch (action)
    {
    case HttpConnection::Action::Read:
        // 注册读事件
        Tools::modfd(m_epollfd, connfd, EPOLLIN, 1);
        break;
    case HttpConnection::Action::Wait:
        // 注册读事件（ET / LT 模式 + oneshot）
        Tools::modfd(m_epollfd, connfd, EPOLLIN, 1);
        break;

    case HttpConnection::Action::Write:
        // 注册写事件
        Tools::modfd(m_epollfd, connfd, EPOLLOUT, 1);
        break;

    case HttpConnection::Action::Close:
        // 清理连接和定时器
        {
            std::lock_guard<std::mutex> lock(m_connections_mutex);
            m_connections[connfd].reset();
        }
        
        // 删除定时器
        if (connfd < (int)m_client_data.size() && m_client_data[connfd].timer && !m_client_data[connfd].timer_deleted) {
            m_client_data[connfd].timer_deleted = true;
            m_timer_manager.del_timer(m_client_data[connfd].timer);
            m_client_data[connfd].timer = nullptr;
        }
        
        // 从 epoll 删除并关闭 fd
        Tools::removefd(m_epollfd, connfd);
        LOG_DEBUG("Connection closed: fd=%d", connfd);
        break;

    default:
        LOG_ERROR("Unknown action for fd %d", connfd);
        break;
    }
}


WebServer::WebServer()
    : m_port(0), m_root(nullptr), m_close_log(0),
      m_epollfd(-1), m_listenfd(-1),
      m_connPool(nullptr),
      m_databaseURL(""), m_user(""), m_passWord(""), m_databaseName(""), m_sql_num(0),
      m_pool(nullptr), m_thread_num(0), m_timeout_sec(15)
{
    m_connections.resize(MAX_FD + 1);
    m_client_data.resize(MAX_FD + 1);
}

WebServer::~WebServer()
{   
    // 1. 停止服务器，防止新连接和新任务
    stop_server = true;
    
    // 2. 关闭监听socket和epoll，停止接受新连接
    if (m_epollfd != -1)
    {
        close(m_epollfd);
        m_epollfd = -1;
    }
    if (m_listenfd != -1)
    {
        close(m_listenfd);
        m_listenfd = -1;
    }
    if (m_pipefd[1] != -1) {
        close(m_pipefd[1]);
        m_pipefd[1] = -1;
    }
    if (m_pipefd[0] != -1) {
        close(m_pipefd[0]);
        m_pipefd[0] = -1;
    }
    
    // 3. 先清理所有连接和定时器，确保主线程不再持有任何连接
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        m_connections.clear();
    }
    
    // 清理所有定时器
    for (auto& client : m_client_data) {
        if (client.timer && !client.timer_deleted) {
            client.timer_deleted = true;
            m_timer_manager.del_timer(client.timer);
            client.timer = nullptr;
        }
    }
    
    // 4. 然后销毁线程池，等待所有工作线程完成
    // 工作线程中的ManagedConnection会自然析构并调用ConnectionPool::release
    if (m_pool)
    {
        delete m_pool;
        m_pool = nullptr;
    }
    
    // 4.5. 清空HTTP连接池，避免double free
    ConnectionPool::get_instance().clear();
    
    // 5. 最后销毁SQL连接池，此时所有ManagedConnection都应该已经析构完成
    if (m_connPool)
    {
        m_connPool->DestroyPool();
        m_connPool = nullptr;
    }
    
    // 释放根目录路径
    if (m_root) {
        free(m_root);
        m_root = nullptr;
    }
}

void WebServer::init(int port, string databaseURL, string user, string passWord, string databaseName,
                     int opt_linger, int sql_num,
                     int thread_num, int close_log, int timeout_sec)
{
    m_port = port;
    m_databaseURL = databaseURL;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_close_log = close_log;
    m_timeout_sec = timeout_sec;
    m_pipefd[0] = -1;
    m_pipefd[1] = -1;
    stop_server = false;

    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init(m_databaseURL, m_user, m_passWord, m_databaseName, 3306, m_sql_num, close_log);

    // 初始化线程池
    m_pool = new thread_pool(m_thread_num, 10000);

    // 日志
    Log::get_instance()->init("./record/ServerLog", m_close_log, 2000, 800000, 1000);
    LOG_INFO("WebServer init: port=%d, LISTENTrigmode=LT, CONNTrigmode=ET, sql_num=%d, thread_num=%d, close_log=%d, actor_model=Reactor",
             m_port, m_sql_num, m_thread_num, m_close_log);

    // root文件夹路径
    char server_path[200];
    if (getcwd(server_path, 200) == nullptr)
    {
        perror("getcwd error");
        exit(1);
    }
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //路由和上下文
    m_context.db_pool = m_connPool;
    m_context.doc_root = m_root;

    // API路由
    m_router.add_route(HttpMethod::GET, "/api/test", handle_simple_json_get);
    m_router.add_route(HttpMethod::POST, "/api/register", handle_register);
    m_router.add_route(HttpMethod::POST, "/api/login", handle_login);
    
    // 静态文件路由 - 使用正则表达式匹配各种文件扩展名
    // 匹配常见的静态文件：html, css, js, 图片, 文档等
    m_router.add_route(HttpMethod::GET, R"(/.*\.(html|htm|css|js|json|txt|xml|csv)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(jpg|jpeg|png|gif|bmp|webp|svg|ico)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(pdf|doc|docx|xls|xlsx|ppt|pptx)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(mp3|wav|mp4|avi)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(zip|tar|gz|rar)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(ttf|woff|woff2)$)", handle_static_file);
    
    // 特殊处理：根目录和无扩展名的文件（如/index, /favicon等）
    m_router.add_route(HttpMethod::GET, R"(^/$)", handle_static_file);  // 根目录重定向到index.html
    m_router.add_route(HttpMethod::GET, R"(/[^.]*$)", handle_static_file);  // 无扩展名文件

}

void WebServer::eventListen()
{
    // 创建监听套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (m_listenfd < 0) { perror("socket error"); exit(EXIT_FAILURE); }

    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    int ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    if (ret < 0) { perror("bind error"); exit(EXIT_FAILURE); }
    ret = listen(m_listenfd, 5);
    if (ret < 0) { perror("listen error"); exit(EXIT_FAILURE); }

    m_epollfd = epoll_create(5);
    if (m_epollfd == -1) { perror("epoll_create error"); exit(EXIT_FAILURE); }
    // 把m_listenfd纳入epoll监听 (LT模式)
    Tools::addfd(m_epollfd, m_listenfd, false, 0);
    std::cout << "HTTP server test running on port " << m_port
              << " model=Reactor"
              << " LISTENTrigmode=LT"
              << " CONNTrigmode=ET" << std::endl;

    Tools::u_epollfd = m_epollfd;      // 设置全局 epoll fd

    // 创建管道用于信号通知
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    if (ret == -1) { perror("socketpair error"); exit(EXIT_FAILURE); }
    Tools::u_pipefd = m_pipefd;
    Tools::setnonblocking(m_pipefd[0]);             // 读端也设非阻塞（推荐）
    Tools::setnonblocking(m_pipefd[1]);             // 写端非阻塞
    Tools::addfd(m_epollfd, m_pipefd[0], false, 0); // 管道通信一般选LT模式
    // 设置信号处理函数
    Tools::addsig(SIGPIPE, SIG_IGN);
    Tools::addsig(SIGINT, Tools::sig_handler, false);
    Tools::addsig(SIGALRM, Tools::sig_handler, false);
    Tools::addsig(SIGTERM, Tools::sig_handler, false);
    
    // 设置定时器信号，每秒触发一次用于检查超时连接
    alarm(1);
}

void WebServer::eventLoop()
{
    while (!stop_server)
    {
        int num = epoll_wait(m_epollfd, events, 10000, -1);
        if (num < 0 && errno != EINTR)
        {
            std::cerr << "epoll failure\n";
            break;
        }
        for (int i = 0; i < num; ++i)
        {
            int sockfd = events[i].data.fd;
            // 处理事件
            if (sockfd == m_listenfd)
            {
                while (true)
                {
                    sockaddr_in client_addr;
                    socklen_t len = sizeof(client_addr);
                    int connfd = accept(m_listenfd, (sockaddr *)&client_addr, &len);
                    if (connfd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept");
                        break;
                    }
                    Tools::addfd(m_epollfd, connfd, true, 1);           // ET模式
                    LOG_INFO("New client connected: %d", connfd); // 日志
                    
                    // 创建HTTP连接对象
                    {
                        std::lock_guard<std::mutex> lock(m_connections_mutex);
                        m_connections[connfd] = std::make_shared<ManagedConnection>(connfd, client_addr, &m_router, &m_context);
                    }
                    
                    // 初始化客户端数据和定时器
                    m_client_data[connfd].address = client_addr;
                    m_client_data[connfd].sockfd = connfd;
                    
                    // 创建定时器
                    util_timer* timer = new util_timer;
                    timer->expire = time(nullptr) + m_timeout_sec;
                    timer->user_data = &m_client_data[connfd];
                    timer->cb_func = [this](client_data* user_data) {
                        // 超时回调：关闭连接并清理资源
                        if (user_data && user_data->sockfd >= 0 && !user_data->timer_deleted) {
                            user_data->timer_deleted = true; // 标记定时器已被删除
                            {
                                std::lock_guard<std::mutex> lock(m_connections_mutex);
                                m_connections[user_data->sockfd].reset();
                            }
                            Tools::removefd(m_epollfd, user_data->sockfd);
                            LOG_DEBUG("Connection timeout and closed: fd=%d", user_data->sockfd);
                            user_data->timer = nullptr; // 清空指针
                        }
                    };
                    
                    m_client_data[connfd].timer = timer;
                    m_client_data[connfd].timer_deleted = false;
                    m_timer_manager.add_timer(timer);
                }
                continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 清理连接和定时器
                {
                    std::lock_guard<std::mutex> lock(m_connections_mutex);
                    m_connections[sockfd].reset();
                }
                
                // 删除定时器
                if (m_client_data[sockfd].timer && !m_client_data[sockfd].timer_deleted) {
                    m_client_data[sockfd].timer_deleted = true;
                    m_timer_manager.del_timer(m_client_data[sockfd].timer);
                    m_client_data[sockfd].timer = nullptr;
                }
                
                epoll_ctl(m_epollfd, EPOLL_CTL_DEL, sockfd, 0);
                LOG_DEBUG("Client disconnected: %d", sockfd);
            }
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                while (true)
                {
                    int sigs[32];
                    int ret = recv(m_pipefd[0], sigs, sizeof(sigs), 0);
                    if (ret <= 0)
                        break; // EAGAIN/0 都跳出
                    int num = ret / sizeof(int);
                    for (int i = 0; i < num; ++i)
                    {
                        switch (sigs[i])
                        {
                        case SIGTERM:
                            stop_server = true;
                            LOG_DEBUG("signal SIGTERM on pipe fd: %d ", sockfd);
                            break;
                        case SIGINT:
                            stop_server = true;
                            LOG_DEBUG("signal SIGINT on pipe fd: %d ", sockfd);
                            break;
                        case SIGALRM:
                            // 处理定时器到期事件
                            m_timer_manager.tick();
                            // 重新设置定时器
                            alarm(1);
                            LOG_DEBUG("Timer tick executed");
                            break;
                        default:
                            std::cerr << "unknown signal " << sigs[i] << std::endl;
                        }
                    }
                }
            }
            else if (events[i].events & EPOLLIN)
            {
                LOG_DEBUG("EPOLLIN event on fd %d", sockfd);
                // Reactor模式
                std::shared_ptr<ManagedConnection> conn_shared;
                {
                    // 加锁的区域非常小，只为了安全地拷贝 shared_ptr
                    std::lock_guard<std::mutex> lock(m_connections_mutex);
                    if (m_connections[sockfd]) {
                        conn_shared = m_connections[sockfd]; // 拷贝，引用计数+1
                    }
                }
                // 如果 conn_shared 有效，才派发任务
                if (conn_shared) {
                    // 更新定时器（延长超时时间）
                    if (m_client_data[sockfd].timer && !m_client_data[sockfd].timer_deleted) {
                        time_t new_expire = time(nullptr) + m_timeout_sec;
                        m_timer_manager.adjust_timer(m_client_data[sockfd].timer, new_expire);
                    }
                    
                    m_pool->append([conn_shared, this, sockfd]() { // 按值捕获 conn_shared
                        HttpConnection::Action action = conn_shared->get()->handle_read();
                        handle_action(sockfd, action);
                    });
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                LOG_DEBUG("EPOLLOUT event on sockfd: %d", sockfd);
                // Reactor模式
                // 在主线程上锁，安全地获取裸指针
                std::shared_ptr<ManagedConnection> conn_shared;
                {
                    // 加锁的区域非常小，只为了安全地拷贝 shared_ptr
                    std::lock_guard<std::mutex> lock(m_connections_mutex);
                    if (m_connections[sockfd]) {
                        conn_shared = m_connections[sockfd]; // 拷贝，引用计数+1
                    }
                }
                // 如果 conn_shared 有效，才派发任务
                if (conn_shared) {
                    // 更新定时器（延长超时时间）
                    if (m_client_data[sockfd].timer && !m_client_data[sockfd].timer_deleted) {
                        time_t new_expire = time(nullptr) + m_timeout_sec;
                        m_timer_manager.adjust_timer(m_client_data[sockfd].timer, new_expire);
                    }
                    
                    m_pool->append([conn_shared, this, sockfd]() { // 按值捕获 conn_shared
                        HttpConnection::Action action = conn_shared->get()->handle_write();
                        handle_action(sockfd, action);
                    });
                }
            }
        }
    }
    // 循环结束时关闭
    if (m_epollfd != -1)
    {
        close(m_epollfd);
        m_epollfd = -1; // 标记为已关闭
    }
    if (m_listenfd != -1)
    {
        close(m_listenfd);
        m_listenfd = -1; // 标记为已关闭
    }
    std::cout << std::endl
              << "Server is shutting down..." << std::endl;
    LOG_INFO("Server is shutting down...");
}
