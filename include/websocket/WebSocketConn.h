#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include "../tools/Tools.h"
#include "../http/Router.h"

class WebSocketServer;

// 在发来的首条消息认证sessionid
class WebSocketConn : public std::enable_shared_from_this<WebSocketConn> {
public:
    WebSocketConn(int fd, WebSocketServer* server, RequestContext* context);
    ~WebSocketConn();

    int fd() const { return fd_; }
    const std::string& username() const { return username_; }
    void set_username(const std::string& name) { username_ = name; }

    // IO: 从 epoll 可读事件中调用
    // 读取 socket 数据并解析 websocket 帧，发现业务消息后调用服务器回调
    // 一般返回Action::Write（如果有数据要发送），否则返回Action::Read
    Action handle_read();

    // IO: 从 epoll 可写事件中调用，尝试把待发送数据写入 socket
    // 一般返回 Action::Close,除非没写完
    Action handle_write();

    // 将文本消息放入发送队列（线程安全）
    void send_text(const std::string& text);

    // 检查写缓冲区是否为空（线程安全）
    bool write_buffer_empty();

    // 重置所有状态（用于连接复用）
    void reset();

    // 设置当收到业务文本消息时的回调（由 server 设定）
    void set_on_message_callback(std::function<void(int fd, const std::string& msg)> cb);

private:
    int fd_;
    WebSocketServer* server_;
    std::string username_;

    // 读缓冲/写缓冲
    std::string read_buf_;
    std::string write_buf_;
    std::mutex write_mtx_;

    // 业务消息回调,int fd, const std::string& msg
    std::function<void(int,const std::string&)> on_message_cb_;

    // 帧处理
    // 解析一个或者多个完整的 text frame（如果缓冲中有），并对每个完成 frame 调用 on_message_cb_
    bool parseFrames();

    // 将一个文本 payload 打包为 websocket frame（单帧，不掩码）
    static std::string pack_text_frame(const std::string& payload);

    // 从客户端接收到的 frame data 包含 mask，需要 unmask
    static void unmaskPayload(char* data, size_t len, const unsigned char mask[4]);

    bool closed_;
};
