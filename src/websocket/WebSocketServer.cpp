#include "websocket/WebSocketServer.h"
#include "websocket/WebSocketConn.h"
#include "log/Log.h"
#include <nlohmann/json.hpp>
#include <chrono>

using json = nlohmann::json;

// 获取单例实例（永不析构）
WebSocketServer& WebSocketServer::getInstance() {
    static WebSocketServer* instance = new WebSocketServer();
    return *instance;
}

WebSocketServer::WebSocketServer() : m_context(nullptr) {
    LOG_INFO("WebSocketServer singleton instance created");
}

void WebSocketServer::setContext(RequestContext* context) {
    m_context = context;
}

void WebSocketServer::registerCallback(int fd, ActionCallback cb) {
    std::lock_guard<std::mutex> lock(mtx_);
    fd_callbacks_[fd] = cb;
    LOG_DEBUG("Registered callback for fd=%d", fd);
}

void WebSocketServer::addConnection(int fd, std::shared_ptr<WebSocketConn> conn) {
    std::lock_guard<std::mutex> lock(mtx_);
    conns_[fd] = conn;
    
    // 设置消息回调
    conn->set_on_message_callback([this](int fd, const std::string& msg) {
        this->handleMessage(fd, msg);
    });
    
    LOG_INFO("WebSocket connection added: fd=%d", fd);
}

void WebSocketServer::removeConnection(int fd) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = conns_.find(fd);
    if (it == conns_.end()) {
        LOG_DEBUG("removeConnection: fd=%d not found in conns_", fd);
        return;
    }
    
    std::string username = it->second->username();
    
    // 打印该连接所在的所有房间
    std::string rooms_str = "";
    for (auto& room_pair : rooms_) {
        if (room_pair.second.find(fd) != room_pair.second.end()) {
            rooms_str += room_pair.first + " ";
        }
    }
    
    LOG_INFO("Removing WebSocket connection: fd=%d, username=%s, rooms=[%s]", 
             fd, username.c_str(), rooms_str.c_str());
    
    // 从所有房间移除
    for (auto& room_pair : rooms_) {
        auto erase_count = room_pair.second.erase(fd);
        if (erase_count > 0) {
            LOG_DEBUG("  Removed fd=%d from room=%s, remaining members=%zu", 
                      fd, room_pair.first.c_str(), room_pair.second.size());
        }
    }
    
    // 从用户映射移除
    if (!username.empty()) {
        users_[username].erase(fd);
        if (users_[username].empty()) {
            users_.erase(username);
        }
    }
    
    // 从连接表中移除连接
    conns_.erase(it);
    
    // 移除回调
    fd_callbacks_.erase(fd);
    
    LOG_INFO("WebSocket connection removed: fd=%d, username=%s", fd, username.c_str());
}

bool WebSocketServer::is_websocket_conn(int fd) {
    std::lock_guard<std::mutex> lock(mtx_);
    return conns_.find(fd) != conns_.end();
}

std::shared_ptr<WebSocketConn> WebSocketServer::getConn(int fd) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = conns_.find(fd);
    if (it != conns_.end()) {
        return it->second;
    }
    return nullptr;
}

void WebSocketServer::joinRoom(const std::string& room, int fd) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = conns_.find(fd);
    if (it == conns_.end()) {
        LOG_WARN("joinRoom: fd=%d not found", fd);
        return;
    }

    rooms_[room].insert(fd);
    LOG_INFO("User fd=%d joined room=%s, new room_size=%zu", fd, room.c_str(), rooms_[room].size());
    
    
    // 广播加入消息
    std::string username = it->second->username();
    if (!username.empty()) {
        json sys_msg = {
            {"type", "system"},
            {"content", username + " joined room " + room},
            {"ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        broadcastRoomLocked(room, sys_msg.dump(), -1);
    }
}

void WebSocketServer::leaveRoom(const std::string& room, int fd) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto room_it = rooms_.find(room);
    if (room_it == rooms_.end()) {
        return;
    }

    auto conn_it = conns_.find(fd);
    if (conn_it != conns_.end()) {
        std::string username = conn_it->second->username();
        
        // 广播离开消息
        if (!username.empty()) {
            json sys_msg = {
                {"type", "system"},
                {"content", username + " left room " + room},
                {"ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            };
            broadcastRoomLocked(room, sys_msg.dump(), -1);
        }
    }
    
    room_it->second.erase(fd);
    
    // 如果房间为空，删除房间
    if (room_it->second.empty()) {
        LOG_INFO("Room %s deleted (empty)", room.c_str());
        rooms_.erase(room_it);
    } 
    
    LOG_INFO("User fd=%d left room=%s", fd, room.c_str());
}

void WebSocketServer::broadcastRoom(const std::string& room, const std::string& message, int exclude_fd) {
    std::lock_guard<std::mutex> lock(mtx_);
    broadcastRoomLocked(room, message, exclude_fd);
}

void WebSocketServer::broadcastRoomLocked(const std::string& room, const std::string& message, int exclude_fd) {
    auto room_it = rooms_.find(room);
    if (room_it == rooms_.end()) {
        LOG_WARN("broadcastRoomLocked: room=%s not found", room.c_str());
        return;
    }
    
    // 打印房间成员
    std::string members = "";
    for (int member_fd : room_it->second) {
        auto member_it = conns_.find(member_fd);
        if (member_it != conns_.end()) {
            members += std::to_string(member_fd) + "(" + member_it->second->username() + ") ";
        } else {
            members += std::to_string(member_fd) + "(not_in_conns) ";
        }
    }
    
    LOG_DEBUG("Broadcasting to room=%s, exclude_fd=%d, room_size=%zu, members=[%s], message=%s", 
              room.c_str(), exclude_fd, room_it->second.size(), members.c_str(), message.c_str());
    
    int broadcast_count = 0;
    for (int fd : room_it->second) {
        if (fd == exclude_fd) {
            LOG_DEBUG("  Skipping excluded fd=%d", fd);
            continue;
        }
        
        auto conn_it = conns_.find(fd);
        if (conn_it != conns_.end()) {
            // 检查写缓冲区是否为空
            bool was_empty = conn_it->second->write_buffer_empty();
            
            // 添加消息到发送缓冲区
            conn_it->second->send_text(message);
            
            LOG_DEBUG("  Broadcasting to fd=%d, username=%s, was_empty=%d", 
                      fd, conn_it->second->username().c_str(), was_empty);
            
            // 只有当写缓冲区从空变为非空时，才触发写事件
            // 如果缓冲区本来就有数据，说明该fd已经在处理写事件或即将处理
            if (was_empty) {
                // 使用该 fd 对应的回调函数（可能来自不同的 SubReactor）
                auto cb_it = fd_callbacks_.find(fd);
                if (cb_it != fd_callbacks_.end()) {
                    cb_it->second(fd, Action::Write);
                    LOG_DEBUG("  Triggered Action::Write for fd=%d", fd);
                } else {
                    LOG_WARN("  No callback registered for fd=%d", fd);
                }
            } else {
                LOG_DEBUG("  Skipped Action::Write for fd=%d (was_empty=%d)", fd, was_empty);
            }
            
            broadcast_count++;
        } else {
            LOG_WARN("  fd=%d in room but not in conns_", fd);
        }
    }
    
    LOG_DEBUG("Broadcast completed: room=%s, sent to %d connections", room.c_str(), broadcast_count);
}

void WebSocketServer::printRoomInfo(const std::string& room, const std::string& context) {
    auto room_it = rooms_.find(room);
    if (room_it == rooms_.end()) {
        LOG_INFO("[%s] Room '%s' not found", context.c_str(), room.c_str());
        return;
    }
    
    std::string members_str = "";
    for (int member_fd : room_it->second) {
        auto member_it = conns_.find(member_fd);
        if (member_it != conns_.end()) {
            members_str += member_it->second->username() + "(fd" + std::to_string(member_fd) + ") ";
        } else {
            members_str += "unknown(fd" + std::to_string(member_fd) + ") ";
        }
    }
    
    LOG_INFO("[%s] Room '%s' has %zu members: %s", 
             context.c_str(), room.c_str(), room_it->second.size(), members_str.c_str());
}

void WebSocketServer::handleMessage(int fd, const std::string& msg) {
    try {
        json j = json::parse(msg);
        std::string type = j.value("type", "");
        
        if (type == "auth") {
            // 认证消息
            handleAuth(fd, j);
        } else if (type == "room") {
            // 房间操作
            handleRoomAction(fd, j);
        } else if (type == "chat") {
            // 聊天消息
            handleChat(fd, j);
        } else {
            LOG_WARN("Unknown message type: %s from fd=%d", type.c_str(), fd);
        }
    } catch (const json::exception& e) {
        LOG_ERROR("JSON parse error from fd=%d: %s, msg=%s", fd, e.what(), msg.c_str());
    }
}

void WebSocketServer::handleAuth(int fd, const json& j) {
    std::string sessionid = j.value("sessionid", "");
    if (sessionid.empty()) {
        LOG_WARN("Auth message missing sessionid from fd=%d", fd);
        json error_msg = {
            {"type", "system"},
            {"content", "Authentication failed: missing sessionid"},
            {"ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        auto conn = getConn(fd);
        if (conn) {
            conn->send_text(error_msg.dump());
            // 触发写事件
            std::lock_guard<std::mutex> lock(mtx_);
            auto cb_it = fd_callbacks_.find(fd);
            if (cb_it != fd_callbacks_.end()) {
                cb_it->second(fd, Action::Write);
            }
        }
        return;
    }
    
    // 从 session 映射表获取用户名
    std::string username;
    {
        std::lock_guard<std::mutex> lock(m_context->session_mtx);
        auto it = m_context->sessions.find(sessionid);
        if (it != m_context->sessions.end()) {
            username = it->second;
        }
    }
    
    if (username.empty()) {
        LOG_WARN("Invalid sessionid from fd=%d", fd);
        json error_msg = {
            {"type", "system"},
            {"content", "Authentication failed: invalid session"},
            {"ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        auto conn = getConn(fd);
        if (conn) {
            conn->send_text(error_msg.dump());
            // 触发写事件
            std::lock_guard<std::mutex> lock(mtx_);
            auto cb_it = fd_callbacks_.find(fd);
            if (cb_it != fd_callbacks_.end()) {
                cb_it->second(fd, Action::Write);
            }
        }
        return;
    }
    
    // 设置用户名
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = conns_.find(fd);
    if (it != conns_.end()) {
        it->second->set_username(username);
        users_[username].insert(fd);
        
        LOG_INFO("WebSocket authenticated: fd=%d, username=%s", fd, username.c_str());
        
        // 发送认证成功消息
        json success_msg = {
            {"type", "system"},
            {"content", "Authentication successful"},
            {"username", username},
            {"ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        it->second->send_text(success_msg.dump());
        // 触发写事件
        auto cb_it = fd_callbacks_.find(fd);
        if (cb_it != fd_callbacks_.end()) {
            cb_it->second(fd, Action::Write);
        }
    }
}

void WebSocketServer::handleRoomAction(int fd, const json& j) {
    std::string action = j.value("action", "");
    std::string room = j.value("room", "");
    
    if (room.empty()) {
        LOG_WARN("Room action missing room name from fd=%d", fd);
        return;
    }
    
    if (action == "join") {
        joinRoom(room, fd);
    } else if (action == "leave") {
        leaveRoom(room, fd);
    } else {
        LOG_WARN("Unknown room action: %s from fd=%d", action.c_str(), fd);
    }
}

void WebSocketServer::handleChat(int fd, const json& j) {
    std::string subtype = j.value("subtype", "");
    LOG_DEBUG("handleChat: fd=%d, subtype=%s, raw_json=%s", fd, subtype.c_str(), j.dump().c_str());
    
    if (subtype == "room_msg") {
        // 房间消息
        std::string room = j.value("room", "");
        std::string content = j.value("content", "");
        std::string from = j.value("from", "");
        
        if (room.empty() || content.empty()) {
            LOG_WARN("Room message missing required fields from fd=%d", fd);
            return;
        }
        
        // 验证发送者
        auto conn = getConn(fd);
        if (!conn || conn->username() != from) {
            LOG_WARN("Username mismatch in room message from fd=%d, conn_username=%s, from=%s", 
                     fd, conn ? conn->username().c_str() : "null", from.c_str());
            return;
        }
        
        LOG_INFO("Received room message: room=%s, from=%s, content=%s", 
                 room.c_str(), from.c_str(), content.c_str());
        
        // 打印当前房间信息
        // {
        //     std::lock_guard<std::mutex> lock(mtx_);
        //     printRoomInfo(room, "handleChat-before-broadcast");
        // }
        
        // 构造转发消息
        json msg = {
            {"type", "chat"},
            {"subtype", "room_msg"},
            {"from", from},
            {"room", room},
            {"content", content},
            {"ts", j.value("ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())}
        };
        
        LOG_DEBUG("Forwarding message: %s", msg.dump().c_str());
        
        // 广播到房间（包括发送者自己）
        broadcastRoom(room, msg.dump(), -1);
        
        LOG_INFO("Room message broadcast completed: room=%s, from=%s", room.c_str(), from.c_str());
    } else {
        LOG_WARN("Unknown chat subtype: %s from fd=%d", subtype.c_str(), fd);
    }
}