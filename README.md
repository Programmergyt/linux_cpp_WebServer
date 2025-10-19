## 编写顺序
lock->log->config->timer->CGImysql->threadpool->http->webserver

## 数据库
string user = "root";
string passwd = "gyt2003gyt";
string databasename = "gytdb";
mysql gytdb -u root -p

## 编译并运行的命令
chmod +x scripts/*
./scripts/build_and_run.sh
./scripts/gdb_debug.sh
./scripts/build_and_test.sh
./scripts/run.sh
./scripts/pressure_test_run.sh
./scripts/git_save.sh
wrk -t12 -c10500 -d5s http://192.168.72.128:8080/index.html
wrk -t12 -c10500 -d5s -s ./scripts/post_login.lua http://192.168.72.128:8080/api/auth/login
wrk -t12 -c10500 -d5s  http://192.168.72.128:8080/api/test

## 改进点
改进点：固定使用reactor，listenfd使用LT,connectfd使用ET
改为主从reactor结构

## 待改进点

待改进点：实现静态文件缓存（如内存缓存或 Redis 集成），减少磁盘 I/O。还是使用redis集成吧，redis适合集群部署。
待改进点：改造为类似于 muduo 库 那样的主从 Reactor 架构，主线程异步唤醒。
待改进点：负载均衡和限流：添加请求限流（rate limiting）以防止 DDoS，使用 token bucket 算法；支持多服务器负载均衡的简单代理。


# 请求示例
POST /2CGISQL.html HTTP/1.1
Host: example.com
Connection: keep-alive
Content-Length: 22
Content-Type: application/x-www-form-urlencoded
\r\n
user=test&passwd=12345

# 成功响应示例
HTTP/1.1 200 OK
Content-Length: 1234
Connection: keep-alive
Content-Type: text/html
\r\n
<html><body>...file content...</body></html>

# 失败响应示例
HTTP/1.1 404 Not Found
Content-Length: 62
Connection: close
Content-Type: text/html
\r\n
The requested file was not found on this server.

压力测试
隔壁最高水平：QPS:6000,TPS:3.7MB
本地最高水平：QPS6600，TPS：4.6MB

重构版
固定预分配内存版本：QPS5000,TPS:6MB
动态分配内存版本：QPS3500,TPS：4.5MB
内存池动态微调版本：QPS4000,TPS：5.2MB
内存池动态微调+定时器版本：QPS3600,TPS：4.8MB

常用指令
wrk -t12 -c10500 -d5s http://192.168.72.128:8080/index.html
wrk -t12 -c10500 -d5s http://192.168.72.128:9006/
cd ../TinyWebServer
cd ../my_cpp
pgrep -f "web_server"
pgrep -f "./server"
./web_server -p <端口号> -l <日志写入方式> -m <触发模式> -o <优雅关闭> -s <数据库连接数> -t <线程数> -c <关闭日志> -a <并发模型>
scp -r C:\\Users\\Thinkbook\\Desktop\\logo.png dick@192.168.72.128:/opt/my_cpp/root/img/ 


HTTP连接类产生doublefree的原因：
C++ 不同编译单元（translation unit）中的静态对象析构顺序是未定义的。
而管理Httpconnection类的Connectionpool是静态对象，http析构时涉及的bufferpool也是静态对象，二者在不同文件，析构顺序不明。如果bufferpool先析构，connectionpool再析构时就会访问到bufferpool为空。


// 定时器封装出工具函数：删除定时器、更新定时器、初始化定时器。定义在tools.h里面。

webserver.h、webserver.cpp、timer.h、timer.cpp

