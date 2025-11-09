#pragma once
#include "../tools/Tools.h" // 包含 Action
#include "../http/Router.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <string>
#include <set>
#include <vector>
#include <functional>
#include <chrono>


class WebSocketConn; // 前向声明

class WebSocketServer {
public:
    // 事件注册回调
    using ActionCallback = std::function<void(int fd, Action)>;

    // 获取单例实例（永不析构）
    static WebSocketServer& getInstance();

    // 禁止拷贝和赋值
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // 设置请求上下文
    void setContext(RequestContext* context);
    
    // 注册某个 SubReactor 的事件回调（每个 fd 需要记录它所属的事件处理回调）
    void registerCallback(int fd, ActionCallback cb);
    
    // 添加连接
    void addConnection(int fd, std::shared_ptr<WebSocketConn> conn);
    
    // 移除连接
    void removeConnection(int fd);

    // 将连接加入房间
    void joinRoom(const std::string& room, int fd);
    void leaveRoom(const std::string& room, int fd);

    // 广播到房间（把消息写入房间内每个websocketconn的写缓冲区，然后将该websocketconn的handle_read和handle_action加入线程池m_pool）
    void broadcastRoom(const std::string& room, const std::string& message, int exclude_fd = -1);
    
    // 判断某 fd 是否为 websocket 连接
    bool is_websocket_conn(int fd);

    // 通过fd获取连接对象
    std::shared_ptr<WebSocketConn> getConn(int fd);

private:
    // 私有构造函数和析构函数
    WebSocketServer();
    ~WebSocketServer() = default;

    // 房间与用户管理
    // fd -> conn
    std::unordered_map<int, std::shared_ptr<WebSocketConn>> conns_;
    // room -> set<fd>
    std::unordered_map<std::string, std::unordered_set<int>> rooms_;
    // username -> set<fd> (一个用户可能多个 socket)
    std::unordered_map<std::string, std::unordered_set<int>> users_;
    // fd -> ActionCallback (每个 fd 对应的事件回调)
    std::unordered_map<int, ActionCallback> fd_callbacks_;
    
    // 互斥锁保护共享数据
    std::mutex mtx_;

    // 供 WebSocketConn 使用的上下文
    RequestContext *m_context;

    // 消息处理
    void handleMessage(int fd, const std::string& msg);
    void handleAuth(int fd, const nlohmann::json& j);
    void handleRoomAction(int fd, const nlohmann::json& j);
    void handleChat(int fd, const nlohmann::json& j);
    
    // 内部广播（需要持有 mtx_）
    void broadcastRoomLocked(const std::string& room, const std::string& message, int exclude_fd);
    
    // 辅助函数：打印房间信息（需要持有 mtx_）
    void printRoomInfo(const std::string& room, const std::string& context);
};
