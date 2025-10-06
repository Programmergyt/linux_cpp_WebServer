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
./scripts/build_and_test.sh
./scripts/run.sh
./scripts/pressure_test_run.sh
./scripts/git_save.sh

## 改进点
改进点：固定使用reactor，listenfd使用LT,connectfd使用ET

## 待改进点
待改进点：http_conn与epoll_fd解耦，不得用assert
待改进点：分解http_conn,保留原有的read,process,write，closefd接口，见gemini的C++ HTTP 连接类优化分析 搜索：重新进行面向对象设计 (分解上帝类)
待改进点：http_conn修改 process() 和 write_once() 的返回值，使用已有的 pipe 机制，与主线程进行通信，根据process和write_once的返回值来决定如何modfd
待改进点：把代码随想录的异步日志系统缝合进来。
待改进点：实现静态文件缓存（如内存缓存或 Redis 集成），减少磁盘 I/O。还是使用redis集成吧，redis适合集群部署。
待改进点：改造为类似于 muduo 库 那样的主从 Reactor 架构 。
待改进点：负载均衡和限流：添加请求限流（rate limiting）以防止 DDoS，使用 token bucket 算法；支持多服务器负载均衡的简单代理。
待改进点：可变大小的读写缓冲区
待改进点：超大文件的传输，并发上传,拆包，包验证，重发，组合
待改进点：压力测试

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


常用指令
wrk -t12 -c10500 -d5s http://192.168.72.128:8080/index.html
wrk -t12 -c10500 -d5s http://192.168.72.128:9006/
cd ../TinyWebServer
cd ../my_cpp
pgrep -f "web_server"
pgrep -f "./server"
./web_server -p <端口号> -l <日志写入方式> -m <触发模式> -o <优雅关闭> -s <数据库连接数> -t <线程数> -c <关闭日志> -a <并发模型>
scp -r C:\\Users\\Thinkbook\\Desktop\\logo.png dick@192.168.72.128:/opt/my_cpp/root/img/ 


