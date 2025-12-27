# C++ 高并发 WebServer
---

# 项目介绍

## 项目概述
这是一个基于 C++17 开发的高性能 Web 服务器，采用主从 Reactor 模式实现。

## 核心特性

### 1. 高性能网络模型
- **主从 Reactor 模式**：主线程负责接受连接，工作线程处理具体请求
- **高效事件处理**：
  - 使用 epoll ET 边缘触发模式
  - listen fd 采用 LT 模式保证连接的可靠性
  - connect fd 采用 ET 模式提高性能
- **线程池优化**：避免频繁创建销毁线程带来的开销

### 2. WebSocket 实时通信
- **WebSocket 协议支持**：完整实现 RFC 6455 WebSocket 协议
- **实时聊天室功能**：支持多房间、多用户实时消息广播
- **单例模式设计**：WebSocketServer 采用永不析构的单例模式，解决多 Reactor 共享数据问题
- **会话管理**：支持基于 sessionid 的用户认证和会话保持
- **消息类型丰富**：支持认证消息、房间管理消息、聊天消息、系统通知等多种消息类型

### 3. 内存管理优化
- **内存池技术**：实现了高效的内存分配策略
- **双缓冲区设计**：用于异步日志系统，提升日志写入性能
- **连接池机制**：管理数据库连接，减少连接建立和断开的开销
- **缓冲区池化**：减少内存碎片，提高内存使用效率

### 4. 工程亮点
- **异步日志系统**：使用双缓冲设计，高效处理日志写入
- **定时器管理**：基于小根堆的定时器，处理超时连接
- **数据库连接池**：管理 MySQL 连接，支持异步数据库操作
- **协议升级机制**：HTTP 连接可平滑升级为 WebSocket 连接

## 性能测试

### 压力测试结果
ubuntu虚拟机（2核心/4GBRAM）
  - QPS: 3600+
  - TPS: 4.8MB/s

### 测试环境
- 测试工具：wrk
- 测试命令：`wrk -t12 -c10500 -d5s http://localhost:8080/`

## 项目结构

### 目录结构
```
.
├── include/             # 头文件
│   ├── config/         # 配置管理
│   ├── handler/        # 请求处理器
│   ├── http/          # HTTP 协议处理
│   ├── log/           # 日志系统
│   ├── sql/           # 数据库连接池
│   ├── thread_pool/   # 线程池
│   ├── timer/         # 定时器
│   ├── tools/         # 工具类
│   ├── webserver/     # 服务器核心
│   └── websocket/     # WebSocket 协议支持
├── src/               # 源代码实现
│   ├── http/         # HTTP 相关实现
│   ├── webserver/    # 服务器实现
│   └── websocket/    # WebSocket 实现
├── scripts/           # 构建和测试脚本
├── test/             # 测试用例
├── root/             # 静态资源目录
│   ├── chat.html     # 聊天室前端页面
│   └── assets/       # 静态资源文件
└── record/           # 服务器日志记录
```

### 核心模块说明

#### 1. 配置模块 (`include/config/`)
- `Config.h`: 服务器配置管理，包括端口号、线程数、数据库连接等参数配置

#### 2. 请求处理模块 (`include/handler/`)
- `Handler.h`: 请求处理器，负责路由分发和具体业务逻辑处理

#### 3. HTTP模块 (`include/http/`)
- `HttpConnection.h`: HTTP 连接类，处理单个 HTTP 连接的完整生命周期
- `HttpConnectionPool.h`: HTTP 连接池，管理连接资源
- `HttpParser.h`: HTTP 请求解析器
- `HttpRequest.h`: HTTP 请求封装
- `HttpResponse.h`: HTTP 响应封装
- `Router.h`: 路由处理
- `BufferPool.h`: 缓冲区池，优化内存分配

#### 4. 日志模块 (`include/log/`)
- `Log.h`: 异步日志系统
- `BlockQueue.h`: 阻塞队列，用于日志异步写入

#### 5. 数据库模块 (`include/sql/`)
- `SqlConnectionPool.h`: MySQL 连接池，管理数据库连接资源

#### 6. 线程池模块 (`include/thread_pool/`)
- `ThreadPool.h`: 线程池实现，支持提交异步任务

#### 7. 定时器模块 (`include/timer/`)
- `Timer.h`: 基于小根堆的定时器，处理超时连接

#### 8. 工具模块 (`include/tools/`)
- `Tools.h`: 通用工具函数，包括字符串处理、时间处理等

#### 9. Web服务器模块 (`include/webserver/`)
- `WebServer.h`: 服务器主类，整合各个模块
- `SubReactor.h`: Reactor 模式实现，处理事件分发

#### 10. WebSocket模块 (`include/websocket/`)
- `WebSocketServer.h`: WebSocket 服务器单例，管理所有 WebSocket 连接和聊天室
- `WebSocketConn.h`: WebSocket 连接类，处理单个 WebSocket 连接的帧解析和消息收发

### 源代码结构
- `src/http/`: HTTP 模块的具体实现
- `src/webserver/`: 服务器核心功能实现
- `src/websocket/`: WebSocket 协议实现和聊天室逻辑

### 资源目录
- `root/`: 存放静态资源文件（HTML、CSS、JavaScript等）
  - `chat.html`: WebSocket 聊天室前端界面
  - `index.html`: 服务器主页
  - `login.html`: 用户登录页面
  - `register.html`: 用户注册页面
- `record/`: 服务器运行日志存储位置

### 脚本说明
- `scripts/`: 包含构建、测试、部署相关的脚本
  - `build_and_run.sh`: 构建并运行服务器
  - `pressure_test_run.sh`: 压力测试脚本
  - `post_login.lua`: 登录接口压测脚本

### 测试相关
- `test/`: 单元测试和集成测试代码

## 构建和运行

### 环境配置

#### 数据库设置
```bash
# 数据库配置信息
user = "root"
passwd = "gyt2003gyt"
databasename = "gytdb"

# 登录数据库
mysql gytdb -u root -p
```

### 构建和运行命令

#### 基本构建
```bash
# 构建项目
mkdir build && cd build
cmake ..
make
```

#### 项目脚本
项目提供了多个便捷脚本，位于 `scripts/` 目录下：
```bash
# 首次运行前，确保脚本有执行权限
chmod +x scripts/*

# 常用脚本说明
./scripts/build_and_run.sh    # 构建并直接运行服务器
./scripts/run.sh              # 直接运行已构建的服务器
./scripts/gdb_debug.sh        # 使用GDB进行调试
./scripts/build_and_test.sh   # 构建并运行测试用例
./scripts/git_save.sh         # 快速提交到Github仓库

# 性能测试相关
./scripts/pressure_test_run.sh # 压力测试模式运行（关闭日志）
```

#### 压力测试命令
```bash
# 测试静态页面性能
wrk -t12 -c105 -d5s http://192.168.72.128:8080/index.html

# 测试登录接口（含数据库访问）
wrk -t12 -c105 -d5s -s ./scripts/post_login.lua http://192.168.72.128:8080/api/auth/login

# 测试普通接口性能
wrk -t12 -c105 -d5s http://192.168.72.128:8080/api/test
```

#### 服务器参数配置
运行服务器时可以通过命令行参数进行配置：
```bash
./web_server [options]

# 选项说明：
# -p <端口号>          监听端口
# -t <线程数>          线程池大小
# -s <数据库连接数>     数据库连接池大小
# -l <日志方式>         日志记录方式
# -m <触发模式>         事件触发模式
# -o                  启用优雅关闭
# -c                  关闭日志
# -a <并发模型>         选择并发处理模型
```

## 技术栈
- 编程语言：C++ 17
- 构建工具：CMake
- 网络模型：Reactor + epoll
- 实时通信：WebSocket (RFC 6455)
- 数据库：MySQL
- JSON 解析：nlohmann/json
- 压测工具：wrk

## 未来规划
1. 引入 Redis 作为缓存层，减少磁盘 I/O【优化】
2. 集成 RabbitMQ 实现任务队列，优化任务处理流程【优化】
3. 实现负载均衡和限流机制（Token Bucket 算法）【优化】
4. 支持分布式部署【优化】
5. 高并发架构设计：利用 RabbitMQ + 线程池，提升系统在多用户场景下的并发处理能力。【优化】
6. 工程化能力：熟悉 Docker 部署、消息队列解耦、数据库异步写入等服务化开发经验。【优化】
7. 添加数据库索引，加快查询速度【优化】


### 技术难点记录

#### HTTP 连接类 double free 问题
**问题描述**：
C++ 不同编译单元（translation unit）中的静态对象析构顺序是未定义的。
而管理 HttpConnection 类的 ConnectionPool 是静态对象，http 析构时涉及的 BufferPool 也是静态对象，二者在不同文件，析构顺序不明。如果 BufferPool 先析构，ConnectionPool 再析构时就会访问到 BufferPool 为空。

**解决方案**：
使用智能指针管理资源，避免直接使用静态对象。

#### WebSocket 消息广播锁粒度问题
**问题描述**：
广播消息时，如果在持有锁的情况下调用回调函数触发写事件，可能导致死锁或性能下降。

**解决方案**：
- 在 `broadcastRoomLocked` 中只持有锁操作共享数据
- 调用 `action_cb_` 触发写事件时，确保不会造成死锁
- 使用 `was_empty` 标志位，只在写缓冲区从空变为非空时才触发写事件，避免重复触发


## 聊天室消息协议设计表

| 消息类型            | 触发场景         | JSON 字段格式                                                                                                                  | 示例               |
| --------------- | ------------ | -------------------------------------------------------------------------------------------------------------------------- | ---------------- |
| **join_room**   | 客户端进入房间（前端发出）      | `{ "type":"room", "action":"join", "room":"room1", "from":"Alice", "ts":1730455200123 }`                                   | Alice的用户进入房间 room1     |
| **leave_room**  | 客户端退出房间（前端发出）      | `{ "type":"room", "action":"leave", "room":"room1", "from":"Alice", "ts":1730455200123 }`                                  | Alice的用户退出房间           |
| **room_msg**    | 群聊消息（前端发出，服务器发给其他成员）   | `{ "type":"chat", "subtype":"room_msg", "from":"Alice", "room":"room1", "content":"Hello everyone!", "ts":1730455200123 }` | Alice用户 向房间内所有人发消息 |
| **system**      | 系统通知（由服务器发出） | `{ "type":"system", "content":"Bob joined room1", "ts":1730455200123 }                                                    | 系统提示消息           |
| **auth**      | onopen验证（由前端发出） | `{ "type": "auth", "sessionid": "abc123" }                                                     | 连接后首条认证消息           |

说明：
* `"type"` 区分主类别（room / chat / system / auth）
* `"subtype"` 区分子类型（room_msg / private_msg）
* `"ts"` 为时间戳（毫秒）

### WebSocket 功能特性

#### 1. 协议升级流程
1. 客户端发起 HTTP 握手请求，包含 WebSocket 升级头部
2. 服务器验证 `Sec-WebSocket-Key`，计算响应的 `Sec-WebSocket-Accept`
3. 发送 101 Switching Protocols 响应完成协议升级
4. 连接从 HTTP 切换为 WebSocket，移出 HTTP 连接池

#### 2. 帧解析机制
- 支持文本帧（Text Frame）和控制帧（Close Frame、Ping/Pong）
- 处理分片消息（Fragmented Messages）
- 解析客户端发送的 Mask 掩码数据
- 支持 Payload 长度的三种编码方式（7-bit、16-bit、64-bit）

#### 3. 聊天室管理
- **房间隔离**：不同房间的消息互不干扰
- **用户管理**：支持一个用户多个连接（多设备登录）
- **实时广播**：消息实时推送到房间内所有在线成员
- **会话认证**：基于 sessionid 的用户身份验证

#### 4. 并发处理
- **多 Reactor 共享**：通过单例模式实现多个 SubReactor 共享聊天室数据
- **线程安全**：使用互斥锁保护共享数据结构
- **事件驱动**：基于 epoll 的异步 I/O 模型
- **连接池化**：WebSocket 连接对象池化管理

#### 5. 错误处理
- 连接异常自动清理
- 超时连接主动断开
- 消息解析错误容错处理
- 房间状态一致性保证


数据库定义：
mysql> select * from users;
+----+-----------------+------------+------------------------------+---------------------+
| id | username        | password   | email                        | create_time         |
+----+-----------------+------------+------------------------------+---------------------+
|  1 | alice           | 123456     | alice@example.com            | 2025-08-27 19:57:37 |
|  2 | bob             | abcdef     | bob@example.com              | 2025-08-27 19:57:37 |
|  3 | niubikelasi     | 12345678   | 2403508140%40qq.com          | 2025-09-02 22:53:34 |
|  9 | 21013204        | gyt2003gyt | 21013204%40mail.ecust.edu.cn | 2025-09-02 23:01:48 |
+----+-----------------+------------+------------------------------+---------------------+

