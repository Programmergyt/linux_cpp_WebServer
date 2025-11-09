#include "websocket/WebSocketServer.h"
#include "websocket/WebSocketConn.h"
#include "log/Log.h"
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <arpa/inet.h>

WebSocketConn::WebSocketConn(int fd, WebSocketServer* server, RequestContext* context)
    : fd_(fd), server_(server), closed_(false) {
    LOG_DEBUG("WebSocketConn created for fd=%d", fd_);
}

WebSocketConn::~WebSocketConn() {
    if (!closed_ && fd_ >= 0) {
        close(fd_);
    }
    LOG_DEBUG("WebSocketConn destroyed for fd=%d", fd_);
}

void WebSocketConn::reset() {
    username_.clear();
    read_buf_.clear();
    write_buf_.clear();
    closed_ = false;
    on_message_cb_ = nullptr;
}

void WebSocketConn::set_on_message_callback(std::function<void(int, const std::string&)> cb) {
    on_message_cb_ = cb;
}

Action WebSocketConn::handle_read() {
    if (closed_) return Action::Close;

    char buffer[4096];
    while (true) {
        ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
        if (n > 0) {
            read_buf_.append(buffer, n);
        } else if (n == 0) {
            // 连接关闭
            LOG_INFO("WebSocket connection closed by peer: fd=%d", fd_);
            closed_ = true;
            return Action::Close;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据读取完毕
                break;
            } else {
                LOG_ERROR("recv error on fd=%d: %s", fd_, strerror(errno));
                closed_ = true;
                return Action::Close;
            }
        }
    }

    // 解析 WebSocket 帧
    if (!parseFrames()) {
        LOG_ERROR("Failed to parse WebSocket frames on fd=%d", fd_);
        closed_ = true;
        return Action::Close;
    }

    // 如果有待发送数据，返回 Write；否则返回 Read
    std::lock_guard<std::mutex> lock(write_mtx_);
    if (!write_buf_.empty()) {
        return Action::Write;
    }
    return Action::Read;
}

Action WebSocketConn::handle_write() {
    if (closed_) return Action::Close;

    std::lock_guard<std::mutex> lock(write_mtx_);
    
    while (!write_buf_.empty()) {
        ssize_t n = send(fd_, write_buf_.c_str(), write_buf_.size(), 0);
        if (n > 0) {
            write_buf_.erase(0, n);
        } else if (n == 0) {
            closed_ = true;
            return Action::Close;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 内核缓冲区满，继续等待写事件
                return Action::Write;
            } else {
                LOG_ERROR("send error on fd=%d: %s", fd_, strerror(errno));
                closed_ = true;
                return Action::Close;
            }
        }
    }

    // 写完毕，切换回读事件
    return Action::Read;
}

void WebSocketConn::send_text(const std::string& text) {
    if (closed_) return;
    
    std::lock_guard<std::mutex> lock(write_mtx_);
    write_buf_.append(pack_text_frame(text));
}

bool WebSocketConn::write_buffer_empty() {
    std::lock_guard<std::mutex> lock(write_mtx_);
    return write_buf_.empty();
}

bool WebSocketConn::parseFrames() {
    while (read_buf_.size() >= 2) {
        unsigned char byte0 = static_cast<unsigned char>(read_buf_[0]);
        unsigned char byte1 = static_cast<unsigned char>(read_buf_[1]);

        bool fin = (byte0 & 0x80) != 0;
        unsigned char opcode = byte0 & 0x0F;
        bool masked = (byte1 & 0x80) != 0;
        uint64_t payload_len = byte1 & 0x7F;

        size_t header_len = 2;

        // 处理扩展的 payload 长度
        if (payload_len == 126) {
            if (read_buf_.size() < 4) return true; // 需要更多数据
            payload_len = (static_cast<unsigned char>(read_buf_[2]) << 8) |
                          static_cast<unsigned char>(read_buf_[3]);
            header_len = 4;
        } else if (payload_len == 127) {
            if (read_buf_.size() < 10) return true;
            payload_len = 0;
            for (int i = 0; i < 8; i++) {
                payload_len = (payload_len << 8) | static_cast<unsigned char>(read_buf_[2 + i]);
            }
            header_len = 10;
        }

        // 处理 mask key
        unsigned char mask_key[4] = {0};
        if (masked) {
            if (read_buf_.size() < header_len + 4) return true;
            for (int i = 0; i < 4; i++) {
                mask_key[i] = static_cast<unsigned char>(read_buf_[header_len + i]);
            }
            header_len += 4;
        }

        // 检查是否有完整帧
        if (read_buf_.size() < header_len + payload_len) {
            return true; // 需要更多数据
        }

        // 提取 payload
        std::string payload = read_buf_.substr(header_len, payload_len);
        read_buf_.erase(0, header_len + payload_len);

        // unmask
        if (masked) {
            unmaskPayload(const_cast<char*>(payload.data()), payload.size(), mask_key);
        }

        // 处理不同的 opcode
        if (opcode == 0x1) {
            // Text frame
            if (on_message_cb_) {
                on_message_cb_(fd_, payload);
            }
        } else if (opcode == 0x8) {
            // Close frame
            LOG_INFO("Received WebSocket close frame on fd=%d", fd_);
            closed_ = true;
            return false;
        } else if (opcode == 0x9) {
            // Ping frame - 回复 pong
            unsigned char pong_header[2] = {0x8A, 0x00}; // FIN=1, opcode=0xA (pong), no payload
            std::lock_guard<std::mutex> lock(write_mtx_);
            write_buf_.append(reinterpret_cast<char*>(pong_header), 2);
        } else if (opcode == 0xA) {
            // Pong frame - 忽略
        } else {
            LOG_WARN("Unsupported WebSocket opcode: 0x%02X on fd=%d", opcode, fd_);
        }
    }

    return true;
}

std::string WebSocketConn::pack_text_frame(const std::string& payload) {
    std::string frame;
    size_t len = payload.size();

    // Byte 0: FIN=1, opcode=1 (text)
    frame.push_back(static_cast<char>(0x81));

    // Byte 1+: payload length (不使用 mask，因为是服务器发送)
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len < 65536) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
        }
    }

    // Payload
    frame.append(payload);

    return frame;
}

void WebSocketConn::unmaskPayload(char* data, size_t len, const unsigned char mask[4]) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= mask[i % 4];
    }
}
