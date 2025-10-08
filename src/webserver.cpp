#include "webserver/webserver.h"


/**
 * @brief 获取文件的MIME类型
 */
static std::string get_mime_type(const std::string& file_path) {
    static const std::unordered_map<std::string, std::string> mime_types = {
        // 文本文件
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".txt", "text/plain"},
        {".xml", "text/xml"},
        {".csv", "text/csv"},
        
        // 图片文件
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".png", "image/png"},
        {".gif", "image/gif"},
        {".bmp", "image/bmp"},
        {".webp", "image/webp"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        
        // 文档文件
        {".pdf", "application/pdf"},
        {".doc", "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls", "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt", "application/vnd.ms-powerpoint"},
        {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        
        // 音视频文件
        {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"},
        {".mp4", "video/mp4"},
        {".avi", "video/x-msvideo"},
        
        // 压缩文件
        {".zip", "application/zip"},
        {".tar", "application/x-tar"},
        {".gz", "application/gzip"},
        {".rar", "application/x-rar-compressed"},
        
        // 字体文件
        {".ttf", "font/ttf"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        
        // 其他
        {".bin", "application/octet-stream"}
    };
    
    // 获取文件扩展名
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "application/octet-stream"; // 默认二进制类型
    }
    
    std::string extension = file_path.substr(dot_pos);
    // 转换为小写
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    auto it = mime_types.find(extension);
    if (it != mime_types.end()) {
        return it->second;
    }
    
    return "application/octet-stream"; // 默认二进制类型
}

/**
 * @brief 处理静态文件请求的handler
 * 支持pdf、jpg、png、css、js、html等常用文件格式
 */
static HttpResponse handle_static_file(const HttpRequest& req, RequestContext& ctx) {
    std::cout << "--- Handler: handle_static_file called for path: " << req.path << " ---\n";
    
    std::string request_path = req.path;
    
    // 特殊处理：根目录重定向到index.html
    if (request_path == "/" || request_path.empty()) {
        request_path = "/index.html";
        std::cout << "[INFO] Root path redirected to /index.html" << std::endl;
    }
    
    // 处理无扩展名的文件，尝试添加.html扩展名
    if (request_path.find('.') == std::string::npos && request_path != "/") {
        std::string html_path = request_path + ".html";
        std::string test_file_path = ctx.doc_root + html_path;
        if (std::filesystem::exists(test_file_path)) {
            request_path = html_path;
            std::cout << "[INFO] No extension file found as HTML: " << request_path << std::endl;
        }
    }
    
    // 构建完整的文件路径
    std::string file_path = ctx.doc_root + request_path;
    
    // 安全检查：防止路径遍历攻击
    std::string canonical_doc_root;
    std::string canonical_file_path;
    
    try {
        canonical_doc_root = std::filesystem::canonical(ctx.doc_root);
        canonical_file_path = std::filesystem::canonical(file_path);
        
        // 检查请求的文件是否在文档根目录下
        if (canonical_file_path.substr(0, canonical_doc_root.length()) != canonical_doc_root) {
            std::cout << "[SECURITY] Path traversal attempt detected: " << req.path << std::endl;
            return HttpResponse::make_error(403); // Forbidden
        }
    } catch (const std::filesystem::filesystem_error& e) {
        // 如果路径不存在或无法解析，返回404
        std::cout << "[ERROR] Filesystem error: " << e.what() << std::endl;
        return HttpResponse::make_error(404);
    }
    
    // 检查文件是否存在且是常规文件
    if (!std::filesystem::exists(canonical_file_path) || 
        !std::filesystem::is_regular_file(canonical_file_path)) {
        std::cout << "[ERROR] File not found or not a regular file: " << canonical_file_path << std::endl;
        return HttpResponse::make_error(404);
    }
    
    // 检查文件权限（可读）
    std::ifstream file(canonical_file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "[ERROR] Cannot open file: " << canonical_file_path << std::endl;
        return HttpResponse::make_error(403); // Forbidden
    }
    
    // 获取文件大小
    std::error_code ec;
    auto file_size = std::filesystem::file_size(canonical_file_path, ec);
    if (ec) {
        std::cout << "[ERROR] Cannot get file size: " << ec.message() << std::endl;
        return HttpResponse::make_error(500);
    }
    
    // 获取MIME类型
    std::string mime_type = get_mime_type(canonical_file_path);
    
    // 创建响应
    HttpResponse response;
    response.status_code = 200;
    response.status_text = "OK";
    response.set_header("Content-Type", mime_type);
    response.set_header("Content-Length", std::to_string(file_size));
    
    // 添加缓存控制头
    if (mime_type.find("image/") == 0 || mime_type.find("font/") == 0 || 
        mime_type == "text/css" || mime_type == "application/javascript") {
        // 静态资源可以缓存较长时间
        response.set_header("Cache-Control", "public, max-age=3600"); // 1小时
    } else {
        // 其他文件缓存时间较短
        response.set_header("Cache-Control", "public, max-age=300"); // 5分钟
    }
    
    // 对于较小的文件（小于1MB），直接读取到内存
    if (file_size < 1024 * 1024) {
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        response.body = std::move(content);
        std::cout << "[SUCCESS] Small file loaded to memory: " << file_size << " bytes" << std::endl;
    } else {
        // 对于大文件，使用文件路径，让HttpConnection使用sendfile发送
        response.file_path = canonical_file_path;
        std::cout << "[SUCCESS] Large file will be sent via sendfile: " << file_size << " bytes" << std::endl;
    }
    
    file.close();
    return response;
}

/**
 * @brief 解析表单数据的辅助函数
 */
static std::string parse_form_field(const std::string& body, const std::string& key) {
    std::string search_key = key + "=";
    size_t pos = body.find(search_key);
    if (pos == std::string::npos) {
        return std::string();
    }
    
    size_t start = pos + search_key.length();
    size_t end = body.find("&", start);
    if (end == std::string::npos) {
        end = body.length();
    }
    
    return body.substr(start, end - start);
}

/**
 * @brief 处理用户注册请求的handler
 */
static HttpResponse handle_register(const HttpRequest& req, RequestContext& ctx) {
    std::cout << "--- Handler: handle_register called ---\n";
    
    // 检查请求方法
    if (req.method != HttpMethod::POST) {
        HttpResponse response;
        response.status_code = 405;
        response.status_text = "Method Not Allowed";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "只支持POST方法"})";
        return response;
    }
    
    // 解析表单数据
    std::string username = parse_form_field(req.raw_body, "username");
    std::string password = parse_form_field(req.raw_body, "password");
    std::string email = parse_form_field(req.raw_body, "email");
    
    std::cout << "[INFO] Register attempt: username=" << username 
              << ", email=" << email << std::endl;
    
    // 检查必填字段
    if (username.empty() || password.empty()) {
        HttpResponse response;
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "用户名和密码不能为空"})";
        return response;
    }
    
    // 数据库操作
    if (ctx.db_pool == nullptr) {
        std::cout << "[ERROR] Database pool is null" << std::endl;
        HttpResponse response;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "数据库连接失败"})";
        return response;
    }
    
    MYSQL* mysql = nullptr;
    connectionRAII mysqlcon(&mysql, ctx.db_pool);
    
    if (mysql == nullptr) {
        std::cout << "[ERROR] Failed to get database connection" << std::endl;
        HttpResponse response;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "数据库连接失败"})";
        return response;
    }
    
    // 构造SQL语句
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO users(username, password, email) VALUES('%s', '%s', '%s')",
             username.c_str(), password.c_str(), email.c_str());
    
    HttpResponse response;
    response.set_header("Content-Type", "application/json");
    
    if (mysql_query(mysql, sql)) {
        std::cout << "[ERROR] MySQL query failed: " << mysql_error(mysql) << std::endl;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.body = R"({"status": "error", "msg": "注册失败"})";
    } else {
        std::cout << "[SUCCESS] User registered successfully: " << username << std::endl;
        response.status_code = 200;
        response.status_text = "OK";
        response.body = R"({"status": "ok", "msg": "注册成功"})";
    }
    
    return response;
}

/**
 * @brief 处理用户登录请求的handler
 */
static HttpResponse handle_login(const HttpRequest& req, RequestContext& ctx) {
    std::cout << "--- Handler: handle_login called ---\n";
    
    // 检查请求方法
    if (req.method != HttpMethod::POST) {
        HttpResponse response;
        response.status_code = 405;
        response.status_text = "Method Not Allowed";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "只支持POST方法"})";
        return response;
    }
    
    // 解析表单数据
    std::string username = parse_form_field(req.raw_body, "username");
    std::string password = parse_form_field(req.raw_body, "password");
    
    std::cout << "[INFO] Login attempt: username=" << username << std::endl;
    
    // 检查必填字段
    if (username.empty() || password.empty()) {
        HttpResponse response;
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "用户名和密码不能为空"})";
        return response;
    }
    
    // 数据库操作
    if (ctx.db_pool == nullptr) {
        std::cout << "[ERROR] Database pool is null" << std::endl;
        HttpResponse response;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "数据库连接失败"})";
        return response;
    }
    
    MYSQL* mysql = nullptr;
    connectionRAII mysqlcon(&mysql, ctx.db_pool);
    
    if (mysql == nullptr) {
        std::cout << "[ERROR] Failed to get database connection" << std::endl;
        HttpResponse response;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "数据库连接失败"})";
        return response;
    }
    
    // 构造SQL查询语句
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id FROM users WHERE username='%s' AND password='%s'",
             username.c_str(), password.c_str());
    
    HttpResponse response;
    response.set_header("Content-Type", "application/json");
    
    if (mysql_query(mysql, sql)) {
        std::cout << "[ERROR] MySQL query failed: " << mysql_error(mysql) << std::endl;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.body = R"({"status": "error", "msg": "登录失败"})";
    } else {
        MYSQL_RES* result = mysql_store_result(mysql);
        if (result == nullptr) {
            std::cout << "[ERROR] Failed to store result: " << mysql_error(mysql) << std::endl;
            response.status_code = 500;
            response.status_text = "Internal Server Error";
            response.body = R"({"status": "error", "msg": "登录失败"})";
        } else {
            my_ulonglong num_rows = mysql_num_rows(result);
            mysql_free_result(result);
            
            if (num_rows == 0) {
                std::cout << "[INFO] Login failed: invalid credentials for " << username << std::endl;
                response.status_code = 401;
                response.status_text = "Unauthorized";
                response.body = R"({"status": "error", "msg": "用户名或密码错误"})";
            } else {
                std::cout << "[SUCCESS] User logged in successfully: " << username << std::endl;
                response.status_code = 200;
                response.status_text = "OK";
                response.body = R"({"status": "ok", "msg": "登录成功"})";
            }
        }
    }
    
    return response;
}

/**
 * @brief 处理简单GET请求返回JSON的handler
 */
static HttpResponse handle_simple_json_get(const HttpRequest& req, RequestContext& ctx) {
    std::cout << "--- Handler: handle_simple_json_get called ---\n";
    std::string json_response = R"({"message": "Hello from HttpConnection", "status": "success"})";
    return HttpResponse()
        .with_status(200, "OK")
        .with_header("Content-Type", "application/json")
        .with_body(json_response);
}

void WebServer::handle_action(int connfd, HttpConnection::Action action)
{
    switch (action)
    {
    case HttpConnection::Action::Read:
    case HttpConnection::Action::Wait:
        // 注册读事件（ET / LT 模式 + oneshot）
        Tools::modfd(m_epollfd, connfd, EPOLLIN, 1);
        break;

    case HttpConnection::Action::Write:
        // 注册写事件
        Tools::modfd(m_epollfd, connfd, EPOLLOUT, 1);
        break;

    case HttpConnection::Action::Close:
        // 从 epoll 删除并关闭 fd
        Tools::removefd(m_epollfd, connfd);
        m_connections[connfd].reset(); // unique_ptr自动delete
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
      m_pool(nullptr), m_thread_num(0)
{
    m_connections.resize(MAX_FD + 1);
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
                    std::cout << "New client connected, fd: " << connfd << std::endl; // 调试信息
                    m_connections[connfd] = std::make_unique<HttpConnection>(connfd, client_addr, &m_router, &m_context);
                }
                continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                m_connections[sockfd]->reset();
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
                m_pool->append([this, sockfd]()
                               {
                                   HttpConnection::Action action = m_connections[sockfd]->handle_read();
                                   handle_action(sockfd, action);
                               });
            }
            else if (events[i].events & EPOLLOUT)
            {
                LOG_DEBUG("EPOLLOUT event on sockfd: %d", sockfd);
                // Reactor模式
                m_pool->append([this, sockfd]()
                {
                    HttpConnection::Action action = m_connections[sockfd]->handle_write();
                    handle_action(sockfd, action);
                });
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
