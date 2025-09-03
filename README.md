编写顺序
lock->log->config->timer->CGImysql->threadpool->http->webserver

数据库
string user = "root";
string passwd = "gyt2003gyt";
string databasename = "gytdb";
 mysql gytdb -u root -p

信号量semaphore操作：wait,post
互斥锁mutex操作：lock,unlock
条件变量condition_variable操作：wait,timewait,signal,broadcast

改进点：把block_queue改为用标准库的mutex和condition_variable实现
改进点：重写log类，去掉宏定义中刚入队可能还没fputs就flush的问题。在主线程退出时通知输出线程让其写完。同时添加m_flush_interval变量，控制刷新频率。
改进点：重构timer和Utils，将Utils中大部分函数以及cb_func迁移至tools作为全局工具函数。原本用链表存储定时器，现在改为用multimap+unordered_map存储定时器，multimap底层用红黑树实现，可自动排序，插入也简单。
改进点：增加线程池优雅退出逻辑，析构函数中有m_stop通知run函数退出，并调用pthread_timedjoin_np完成超时等待。将线程池与http_conn与sql_connect解耦，实现了更通用的线程池。

待改进点：命令行参数输入，区分listenfd和connfd的Trigmode

待改进点：做出完整的网站示例，能够发出各种请求。服务器处理时能调用数据库。

请求示例
POST /2CGISQL.html HTTP/1.1
Host: example.com
Connection: keep-alive
Content-Length: 22
Content-Type: application/x-www-form-urlencoded
\r\n
user=test&passwd=12345

成功响应示例
HTTP/1.1 200 OK
Content-Length: 1234
Connection: keep-alive
Content-Type: text/html
\r\n
<html><body>...file content...</body></html>

失败响应示例
HTTP/1.1 404 Not Found
Content-Length: 62
Connection: close
Content-Type: text/html
\r\n
The requested file was not found on this server.

压力测试
隔壁最高水平：QPS:6000,TPS:3.7MB
本地最高水平：QPS6600，TPS：4.6MB

常用指令
wrk -t12 -c10500 -d5s http://192.168.72.128:8080/index.html
wrk -t12 -c10500 -d5s http://192.168.72.128:9006/
cd ../TinyWebServer
cd ../my_cpp
pgrep -f "web_server"
pgrep -f "./server"
./web_server -p <端口号> -l <日志写入方式> -m <触发模式> -o <优雅关闭> -s <数据库连接数> -t <线程数> -c <关闭日志> -a <并发模型>
scp -r C:\\Users\\Thinkbook\\Desktop\\logo.png dick@192.168.72.128:/opt/my_cpp/root/img/ 


