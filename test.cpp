#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mysql/mysql.h>
#include "lock/locker.h"
#include "log/block_queue.h"
#include "log/log.h"
#include "config/config.h"
#include "sql/sql_connection_pool.h"
#include "timer/timer.h"
#include "thread_pool/thread_pool.h"
#include "http/http_conn.h"

block_queue<int> q(5);

void* producer(void* arg) {
    for (int i = 0; i < 10; ++i) {
        q.push(i);
        std::cout << "Produced: " << i << std::endl;
        usleep(100000); // 0.1s
    }
    return nullptr;
}

void* consumer(void* arg) {
    int val;
    for (int i = 0; i < 10; ++i) {
        if (q.pop(val)) {
            std::cout << "Consumed: " << val;
            std::cout << " Queue size: " << q.size();
            int front_val=-99, back_val=-99;
            q.front(front_val);
            q.back(back_val);
            std::cout << " back: " << back_val;
            std::cout << " front: " << front_val << std::endl;
        }
        usleep(150000); // 0.15s
    }
    return nullptr;
}

void test_block_queue() {
    std::cout << "生产者-消费者模型下的block_queue" << std::endl;
    pthread_t t1, t2;
    pthread_create(&t1, nullptr, producer, nullptr);
    pthread_create(&t2, nullptr, consumer, nullptr);

    pthread_join(t1, nullptr);
    pthread_join(t2, nullptr);
    std::cout << "block_queue基本操作" << std::endl;
    block_queue<int> bq(3);
    int val;
    bq.push(1);
    bq.push(2);
    bq.push(3);
    if (bq.full()) {
        std::cout << "Queue is full." << std::endl;
    }
    bq.pop(val,1);
    std::cout << "Popped: " << val << std::endl;
    if (!bq.empty()) {
        std::cout << "Queue is not empty." << std::endl;
    }
    bq.clear();
    if (bq.empty()) {
        std::cout << "Queue cleared." << std::endl;
    }
}

void test_log() {

    Log::get_instance()->init("./record/ServerLog", 0, 2000, 800000, 1000);
    while(true)
    {
        sleep(1); // 模拟日志生成间隔
        LOG_INFO("This is an info log.");
        LOG_DEBUG("This is a debug log.");
        LOG_WARN("This is a warning log."); 
        LOG_ERROR("This is an error log.");
        LOG_INFO("Log test completed.%s %d","wocaosinidema",1);
    }

}

void test_config()
{
    std::cout << "测试config类" << std::endl;
    Config config;
    // 定义一个真正的数组
    char* argv[] = {
        (char*)"program",
        (char*)"-p",
        (char*)"8080",
        (char*)"-l",
        (char*)"1",
        (char*)"-m",
        (char*)"2",
        (char*)"-o",
        (char*)"1"
    };

    // 数组元素个数
    int argc = sizeof(argv) / sizeof(argv[0]);

    // 调用函数
    config.parse_arg(argc, argv);
    std::cout << "PORT: " << config.PORT << std::endl;
    std::cout << "LOGWrite: " << config.LOGWrite << std::endl;
    std::cout << "TRIGMode: " << config.TRIGMode << std::endl;
    std::cout << "OPT_LINGER: " << config.OPT_LINGER << std::endl;
}

// 测试函数，演示添加/调整/执行定时器
void test_timer() {
    timer_manager tm;

    // 模拟客户端数据
    auto* client = new client_data;
    client->sockfd = 1; // 只是演示用

    // 定时器1：3秒后触发
    auto* t1 = new util_timer;
    t1->expire = time(nullptr) + 3;
    t1->user_data = client;
    t1->cb_func = [](client_data* data) {
        std::cout << "[Timer 1] expired, closing sockfd=" << data->sockfd << "\n";
    };
    tm.add_timer(t1);

    // 定时器2：5秒后触发
    auto* t2 = new util_timer;
    t2->expire = time(nullptr) + 5;
    t2->user_data = client;
    t2->cb_func = [](client_data* data) {
        std::cout << "[Timer 2] expired, cleaning sockfd=" << data->sockfd << "\n";
    };
    tm.add_timer(t2);

    // 模拟循环，tick 每秒执行一次
    for (int i = 0; i < 7; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "[Tick] second " << i+1 << "\n";
        tm.tick();

        // 在第2秒时，延长定时器1到 6 秒后触发
        if (i == 1) {
            std::cout << "[Adjust] 延长 Timer 1 到 6 秒后\n";
            tm.adjust_timer(t1, time(nullptr) + 4);
        }
    }
}

void test_sql_connection_pool()
{
    // 直接使用连接池
    connection_pool* pool = connection_pool::GetInstance();
    pool->init("localhost","root", "gyt2003gyt", "gytdb", 3306, 10, 0);
    MYSQL* conn = pool->GetConnection();
    if (conn) {
        std::cout << "Successfully got a connection from the pool." << std::endl;
        // 这里可以执行一些数据库操作
        mysql_query(conn, "SELECT * FROM user");
        MYSQL_RES* res = mysql_store_result(conn);
        if (res) {
            int num_fields = mysql_num_fields(res);
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                for (int i = 0; i < num_fields; i++) {
                    std::cout << (row[i] ? row[i] : "NULL") << " ";
                }
                std::cout << std::endl;
            }
            mysql_free_result(res);
        } else {
            std::cerr << "mysql_store_result() failed: " << mysql_error(conn) << std::endl;
        }
        pool->ReleaseConnection(conn);
        std::cout << "Connection released back to the pool." << std::endl;
    } else {
        std::cout << "Failed to get a connection from the pool." << std::endl;
    }
    pool->DestroyPool();
    std::cout << "Connection pools destroyed." << std::endl;

    // 使用 RAII 管理连接池,必须先有初始化完成的连接池
    connection_pool* pool2 = connection_pool::GetInstance();// 获取单例连接池
    pool2->init("localhost","root", "gyt2003gyt", "gytdb", 3306, 10, 0);
    MYSQL* conn2 = nullptr;// 定义连接
    // 使用RAII省去ReleaseConnection的调用
    connectionRAII connRAII(&conn2, pool2);
    if (conn2) {
        std::cout << "Successfully got a connection using RAII." << std::endl;
        mysql_query(conn2, "SELECT * FROM user");
        MYSQL_RES* res = mysql_store_result(conn2);
        if (res) {
            int num_fields = mysql_num_fields(res);
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                for (int i = 0; i < num_fields; i++) {
                    std::cout << (row[i] ? row[i] : "NULL") << " ";
                }
                std::cout << std::endl;
            }
            mysql_free_result(res);
        } else {
            std::cerr << "mysql_store_result() failed: " << mysql_error(conn2) << std::endl;
        }
    } else {
        std::cout << "Failed to get a connection using RAII." << std::endl;
    }
    pool2->DestroyPool();
    std::cout << "Connection pools destroyed using RAII." << std::endl;
}

void test_thread_pool() {
    // 线程池测试代码
    // 这里可以添加线程池的初始化和任务提交等操作
    std::cout << "Thread pool test is not implemented yet." << std::endl;
    thread_pool pool(4, 100,5); // 4 个线程，最大 100 个任务，超时等待 5 秒

    std::cout << "==== 使用 lambda 投递任务 ====" << std::endl;
    for (int i = 0; i < 5; i++) {
        pool.append([i]() {
            std::cout << "Lambda Task " << i 
                      << " is running in thread " 
                      << pthread_self() << std::endl;
        });
    }
    // 等一会儿让线程跑完
    sleep(2);
}

void test_http_conn(int port = 8080, int actor_model = 0, int TRIGMode = 0) {
    std::cout << "HTTP server test running on port " << port
              << " model=" << (actor_model==0?"Proactor":"Reactor")
              << " trig=" << (TRIGMode==0?"LT":"ET") << std::endl;

    Log::get_instance()->init("./record/ServerLog", 0, 2000, 800000, 1000);
    static char doc_root[] = "./root";

    // 线程池
    thread_pool pool(4, 100);

    // 监听 socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    assert(bind(listenfd, (struct sockaddr*)&address, sizeof(address)) >= 0);
    assert(listen(listenfd, 8) >= 0);

    // epoll
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    Tools::addfd(epollfd, listenfd, false, TRIGMode);

    // http_conn 数组（按 fd 存储）
    http_conn::m_epoll_fd = epollfd; // 设置全局 epoll fd
    std::vector<http_conn> users(65536);

    epoll_event events[10000];

    while (true) 
    {
        int num = epoll_wait(epollfd, events, 10000, -1);
        if (num < 0 && errno != EINTR) 
        {
            std::cerr << "epoll failure\n";
            break;
        }
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;

            if (sockfd == listenfd) 
            {
                // 新连接
                sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int connfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_len);
                if (connfd < 0) continue;
                Tools::addfd(epollfd, connfd, true, TRIGMode);
                users[connfd].init(connfd,client_addr, doc_root, TRIGMode, 0);
                users[connfd].set_actor_model(actor_model);
                std::cout << "New connection: " << connfd << std::endl;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) 
            {
                // 客户端关闭或出错
                users[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN) 
            {
                std::cout << "EPOLLIN event on sockfd: " << sockfd << std::endl;
                if (actor_model == 0) 
                { // Proactor
                    if (users[sockfd].read_once()) 
                    {
                        // std::cout << "Read data from sockfd: " << sockfd << std::endl;
                        pool.append([&, sockfd]() {
                            users[sockfd].process();
                            // std::cout << "Processed request for sockfd: " << sockfd << std::endl;
                        });
                    } 
                    else 
                    {
                        users[sockfd].close_conn();
                    }
                } 
                else 
                { // Reactor
                    users[sockfd].set_io_state(http_conn::IO_READ);
                    pool.append([&, sockfd]() 
                    {
                        if (users[sockfd].read_once()) {
                            users[sockfd].process();
                        } else {
                            users[sockfd].close_conn();
                        }
                    });
                }
            }
            else if (events[i].events & EPOLLOUT) 
            {
                std::cout << "EPOLLOUT event on sockfd: " << sockfd << std::endl;
                if (actor_model == 0) 
                { // Proactor
                    if (!users[sockfd].write_once()) 
                    {
                        users[sockfd].close_conn();
                    } 
                    else 
                    {
                        if (users[sockfd].keep_alive())
                            Tools::modfd(epollfd, sockfd, EPOLLIN, TRIGMode);
                        else
                            users[sockfd].close_conn();
                    }
                } 
                else 
                { // Reactor
                    pool.append([&, sockfd]() 
                    {
                        users[sockfd].set_io_state(http_conn::IO_WRITE);
                        if (!users[sockfd].write_once()) 
                        {
                            users[sockfd].close_conn();
                        } 
                        else 
                        {
                            if (users[sockfd].keep_alive())
                                Tools::modfd(epollfd, sockfd, EPOLLIN, TRIGMode);
                            else
                                users[sockfd].close_conn();
                        }
                    });
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
}


int main(int argc, char* argv[]) {
    // test_block_queue();
    // test_log();
    // test_config();
    // test_timer();
    // test_sql_connection_pool();
    // test_thread_pool();
    test_http_conn(8080,1,0);
    return 0;
}
