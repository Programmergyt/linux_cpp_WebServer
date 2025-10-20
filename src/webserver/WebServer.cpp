#include "webserver/WebServer.h"
#include "handler/Handler.h"
#include "http/HttpConnectionPool.h"
#include <thread>

WebServer::WebServer()
    : m_port(0), m_root(nullptr), m_close_log(0),
      m_epollfd(-1), m_listenfd(-1),
      m_connPool(nullptr),
      m_databaseURL(""), m_user(""), m_passWord(""), m_databaseName(""), m_sql_num(0),
      m_pool(nullptr), m_thread_num(0), m_timeout_sec(15),
      m_sub_reactor_num(0), m_round_robin_counter(0) // 初始化新成员
{}

WebServer::~WebServer()
{
    // 1. 停止服务器，设置标志
    stop_server = true;
    
    // 2. 向所有 SubReactor 发送停止信号
    int stop_msg = -1; // -1 代表停止
    for (auto* sub : m_sub_reactors) {
        if (sub) {
            // 写入管道以唤醒 SubReactor 的 epoll_wait
            write(sub->getPipeFd(), &stop_msg, sizeof(stop_msg));
        }
    }

    // 3. 等待所有 SubReactor 线程结束
    for (auto& th : m_sub_threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    // 4. 删除 SubReactor 实例
    for (auto* sub : m_sub_reactors) {
        delete sub;
    }

    // 5. 关闭主 Reactor 的 epoll 和 fds
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
    
    // 6. 销毁共享的工作线程池
    if (m_pool)
    {
        delete m_pool;
        m_pool = nullptr;
    }
    
    // 7. 销毁共享的SQL连接池
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
                     int sql_num,int thread_num, int close_log, int timeout_sec)
{
    m_port = port;
    m_databaseURL = databaseURL;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num; // 这是 *工作* 线程池的线程数
    m_close_log = close_log;
    m_timeout_sec = timeout_sec;
    m_pipefd[0] = -1;
    m_pipefd[1] = -1;
    stop_server = false;
    m_round_robin_counter = 0;

    // 1. 初始化数据库连接池 (共享)
    m_connPool = SqlConnectionPool::GetInstance();
    m_connPool->init(m_databaseURL, m_user, m_passWord, m_databaseName, 3306, m_sql_num, close_log);

    // 2. 初始化 *工作* 线程池 (共享)
    m_pool = new ThreadPool(m_thread_num, 10000);

    // 3. 初始化日志
    Log::get_instance()->init("./record/ServerLog", m_close_log, 2000, 800000, 1000);
    LOG_INFO("WebServer (MainReactor) init...");

    // 4. 初始化 root 路径
    char server_path[200];
    if (getcwd(server_path, 200) == nullptr) { perror("getcwd error"); exit(EXIT_FAILURE); }
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 5. 初始化路由和上下文 (共享)
    m_context.db_pool = m_connPool;
    m_context.doc_root = m_root;

    // API 路由
    m_router.add_route(HttpMethod::GET, "/api/test", handle_simple_json_get);
    m_router.add_route(HttpMethod::POST, "/api/register", handle_register);
    m_router.add_route(HttpMethod::POST, "/api/login", handle_login);
    // 静态文件路由 - 使用正则表达式匹配各种文件扩展名
    m_router.add_route(HttpMethod::GET, R"(/.*\.(html|htm|css|js|json|txt|xml|csv)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(jpg|jpeg|png|gif|bmp|webp|svg|ico)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(pdf|doc|docx|xls|xlsx|ppt|pptx)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(mp3|wav|mp4|avi)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(zip|tar|gz|rar)$)", handle_static_file);
    m_router.add_route(HttpMethod::GET, R"(/.*\.(ttf|woff|woff2)$)", handle_static_file);
    // 特殊处理：根目录和无扩展名的文件（如/index, /favicon等）
    m_router.add_route(HttpMethod::GET, R"(^/$)", handle_static_file);  // 根目录重定向到index.html
    m_router.add_route(HttpMethod::GET, R"(/[^.]*$)", handle_static_file);  // 无扩展名文件


    // 6. 初始化从 Reactors
    m_sub_reactor_num = std::thread::hardware_concurrency();
    if (m_sub_reactor_num <= 0) 
        m_sub_reactor_num = 8; // 备用
    
    m_sub_reactors.resize(m_sub_reactor_num, nullptr);
    m_sub_threads.resize(m_sub_reactor_num);

    LOG_INFO("MainReactor: Creating %d SubReactors and %d Worker Threads.", m_sub_reactor_num, m_thread_num);

    for (int i = 0; i < m_sub_reactor_num; ++i)
    {
        m_sub_reactors[i] = new SubReactor();
        // 将共享资源传递给 SubReactor
        m_sub_reactors[i]->init(m_pool, m_connPool, &m_router, &m_context, m_timeout_sec, &stop_server);
        // 启动 SubReactor 线程
        m_sub_threads[i] = std::thread(&SubReactor::eventLoop, m_sub_reactors[i]);
    }
}

void WebServer::eventListen()
{
    // 创建监听套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (m_listenfd < 0) { perror("socket error：创建监听套接字失败"); exit(EXIT_FAILURE); }

    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    int ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    if (ret < 0) { perror("bind error: 端口已经被占用"); exit(EXIT_FAILURE); }
    ret = listen(m_listenfd, 5);
    if (ret < 0) { perror("listen error: 监听失败"); exit(EXIT_FAILURE); }

    // 主 Reactor 的 epoll
    m_epollfd = epoll_create(5);
    if (m_epollfd == -1) { perror("epoll_create error: Epoll创建失败"); exit(EXIT_FAILURE); }

    // 只把 m_listenfd 纳入主 epoll 监听 (LT模式)
    Tools::addfd(m_epollfd, m_listenfd, false, 0);
    
    std::cout << "HTTP server (Main-Sub Reactor) running on port " << m_port
              << " | SubReactors=" << m_sub_reactor_num
              << " | WorkerThreads=" << m_thread_num
              << std::endl;

    // 创建 主 Reactor 的信号管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    if (ret == -1) { std::cerr << "socketpair error" << std::endl; exit(EXIT_FAILURE); }
    Tools::u_pipefd = m_pipefd; // Tools 静态成员指向此管道
    Tools::setnonblocking(m_pipefd[0]);
    Tools::setnonblocking(m_pipefd[1]);
    
    // 只把 主 Reactor 的信号管道读端加入 主 epoll
    Tools::addfd(m_epollfd, m_pipefd[0], false, 0); // LT模式

    // 设置信号处理函数
    Tools::addsig(SIGPIPE, SIG_IGN);
    Tools::addsig(SIGINT, Tools::sig_handler, false);
    Tools::addsig(SIGALRM, Tools::sig_handler, false); // 主 Reactor 接收 SIGALRM
    Tools::addsig(SIGTERM, Tools::sig_handler, false);
    
    // 设置定时器信号
    alarm(1);
}

void WebServer::eventLoop()
{
    LOG_INFO("MainReactor: Event loop starting...");
    while (!stop_server)
    {
        // 等待 m_listenfd 和 m_pipefd[0]
        int num = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (num < 0 && errno != EINTR)
        {
            LOG_ERROR("MainReactor: epoll failure");
            break;
        }
        for (int i = 0; i < num; ++i)
        {
            int sockfd = events[i].data.fd;

            // 1. 处理新连接
            if (sockfd == m_listenfd)
            {
                LOG_DEBUG("MainReactor: Accepting new connection...");
                while (true) // LT模式，循环 accept
                {
                    sockaddr_in client_addr;
                    socklen_t len = sizeof(client_addr);
                    int connfd = accept(m_listenfd, (sockaddr *)&client_addr, &len);
                    if (connfd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break; // 没有更多连接了
                        perror("accept");
                        break;
                    }

                    // 轮询分发给 SubReactor
                    int sub_index = m_round_robin_counter % m_sub_reactor_num;
                    m_round_robin_counter++;
                    
                    int pipe_fd = m_sub_reactors[sub_index]->getPipeFd();
                    int msg = connfd; // 消息就是 connfd
                    
                    // 将 connfd 写入 SubReactor 的管道，由SubReactor处理
                    write(pipe_fd, &msg, sizeof(msg));
                    
                    LOG_DEBUG("MainReactor: Dispatched fd %d to SubReactor %d", connfd, sub_index);
                }
                continue;
            }
            // 2. 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                while (true)
                {
                    int sigs[32];
                    int ret = recv(m_pipefd[0], sigs, sizeof(sigs), 0);
                    if (ret <= 0)
                        break; // EAGAIN/0 都跳出
                    
                    int num_sigs = ret / sizeof(int);
                    for (int i = 0; i < num_sigs; ++i)
                    {
                        switch (sigs[i])
                        {
                        case SIGTERM:
                        case SIGINT:
                            {
                                LOG_INFO("MainReactor: Stop signal received. Notifying SubReactors.");
                                stop_server = true;
                                int stop_msg = -1; // -1 代表停止
                                for (auto* sub : m_sub_reactors) {
                                    write(sub->getPipeFd(), &stop_msg, sizeof(stop_msg));
                                }
                                break;
                            }
                        case SIGALRM:
                            {
                                LOG_DEBUG("MainReactor: Timer tick received. Forwarding to SubReactors.");
                                int tick_msg = -2; // -2 代表 Tick
                                for (auto* sub : m_sub_reactors) {
                                    write(sub->getPipeFd(), &tick_msg, sizeof(tick_msg));
                                }
                                // 重新设置定时器
                                alarm(1);
                                break;
                            }
                        default:
                            std::cerr << "unknown signal " << sigs[i] << std::endl;
                        }
                    }
                }
            }
            // 主 Reactor 不应收到其他事件，防御性编程
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                LOG_ERROR("MainReactor: Unexpected event (ERR/HUP) on fd %d", sockfd);
                close(sockfd); 
            }
        } 
    } 

    // 循环结束时
    LOG_INFO("MainReactor: Event loop stopped. Server is shutting down...");
    std::cout << std::endl<<"服务器正在关闭..." << std::endl;
    // 析构函数将处理线程 join 和资源清理
}