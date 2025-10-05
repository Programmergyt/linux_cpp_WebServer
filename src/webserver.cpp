#include "webserver/webserver.h"

WebServer::WebServer()
    : m_port(0), m_root(nullptr), m_close_log(0),
      m_epollfd(-1), m_listenfd(-1),
      m_connPool(nullptr),
      m_databaseURL(""), m_user(""), m_passWord(""), m_databaseName(""), m_sql_num(0),
      m_pool(nullptr), m_thread_num(0)
{
    users = new http_conn[MAX_FD];
}

WebServer::~WebServer()
{
    if (m_epollfd != -1)
    {
        close(m_epollfd);
    }
    if (m_listenfd != -1)
    {
        close(m_listenfd);
    }
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    if (users)
    {
        delete[] users;
        users = nullptr;
    }
    if (m_pool)
    {
        delete m_pool;
        m_pool = nullptr;
    }
    if (m_connPool)
    {
        m_connPool->DestroyPool();
        m_connPool = nullptr;
    }
}

void WebServer::init(int port, string databaseURL, string user, string passWord, string databaseName,
                     int opt_linger, int sql_num,
                     int thread_num, int close_log)
{
    m_port = port;
    m_databaseURL = databaseURL;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_close_log = close_log;
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

    http_conn::m_epoll_fd = m_epollfd; // 设置全局 epoll fd
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
}

void WebServer::eventLoop()
{
    bool timeout = false;
    // 定时器：每 5 秒触发一次 SIGALRM
    alarm(TIMESLOT);
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
                    users[connfd].init(connfd, client_addr, m_root, 0);    // 固定ET模式
                    // 绑定定时器
                    users[connfd].set_actor_model(1); // Reactor模式
                    users[connfd].set_connection_pool(m_connPool);
                    LOG_INFO("New connection from %s:%d, assigned to fd %d",
                             inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), connfd);
                    // === 定时器绑定 ===
                    users_timer[connfd].address = client_addr;
                    users_timer[connfd].sockfd = connfd;

                    util_timer *timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = [](client_data *user_data)
                    {
                        epoll_ctl(Tools::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
                        if (user_data == nullptr) return;
                        close(user_data->sockfd);
                        LOG_INFO("closed fd=%d due to timeout", user_data->sockfd);
                    };
                    timer->expire = time(nullptr) + 3 * TIMESLOT;

                    users_timer[connfd].timer = timer;
                    m_timer_mgr.add_timer(timer);
                }
                continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 客户端关闭或出错
                if (users_timer[sockfd].timer)
                {
                    m_timer_mgr.del_timer(users_timer[sockfd].timer);
                    users_timer[sockfd].timer = nullptr;
                }
                users[sockfd].close_conn();
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
                        case SIGALRM:
                            timeout = true; // 定时器标志，事件到了即置位
                            LOG_DEBUG("signal SIGALRM on pipe fd: %d ", sockfd);
                            break;
                        case SIGINT:
                            stop_server = true;
                            LOG_DEBUG("signal SIGINT on pipe fd: %d ", sockfd);
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
                if (users_timer[sockfd].timer)
                {
                    m_timer_mgr.adjust_timer(users_timer[sockfd].timer, time(nullptr) + 3 * TIMESLOT);
                    LOG_INFO("reset timer for fd %d", sockfd);
                }
                // Reactor模式
                users[sockfd].set_io_state(http_conn::IO_READ);
                m_pool->append([this, sockfd]()
                               {
                    if (users[sockfd].read_once()) {
                        users[sockfd].process();
                    } else {
                        users[sockfd].close_conn();
                    } });
            }
            else if (events[i].events & EPOLLOUT)
            {
                LOG_DEBUG("EPOLLOUT event on sockfd: %d", sockfd);
                if (users_timer[sockfd].timer)
                {
                    m_timer_mgr.adjust_timer(users_timer[sockfd].timer, time(nullptr) + 3 * TIMESLOT);
                    LOG_INFO("reset timer for fd %d", sockfd);
                }
                // Reactor模式
                m_pool->append([this, sockfd]()
                               {
                    users[sockfd].set_io_state(http_conn::IO_WRITE);
                    if (!users[sockfd].write_once()) 
                    {
                        users[sockfd].close_conn();
                    } 
                    else 
                    {
                        if (users[sockfd].bytes_to_send() <= 0) 
                        {
                            if (users[sockfd].keep_alive())
                                {
                                Tools::modfd(m_epollfd, sockfd, EPOLLIN, 1); // ET模式
                                LOG_DEBUG("keep alive, switch to EPOLLIN for fd %d", sockfd);
                                }
                            else
                                users[sockfd].close_conn();
                        } else {
                            // 保持EPOLLOUT，继续发
                            Tools::modfd(m_epollfd, sockfd, EPOLLOUT, 1); // ET模式
                        }
                    } });
            }
            // m_timer_mgr.tick();// 定时器刷新,要定时精度则取消注释
        }
        // 处理定时器 tick
        if (timeout)
        {
            m_timer_mgr.tick(); // 遍历定时器，关闭超时连接
            alarm(TIMESLOT);    // 重新定时
            timeout = false;
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
