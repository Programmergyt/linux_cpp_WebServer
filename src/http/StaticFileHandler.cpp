// StaticFileHandler.cpp
#include "http/StaticFileHandler.h"
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <algorithm>
#include <cctype>

// MIME类型映射表
const std::unordered_map<std::string, std::string> StaticFileHandler::mime_types = {
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".txt", "text/plain"},
    {".xml", "text/xml"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".bmp", "image/bmp"},
    {".ico", "image/x-icon"},
    {".svg", "image/svg+xml"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},
    {".mp3", "audio/mpeg"},
    {".wav", "audio/wav"},
    {".mp4", "video/mp4"},
    {".avi", "video/x-msvideo"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".xls", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"}
};

StaticFileHandler::StaticFileHandler(const std::string& doc_root) 
    : m_doc_root(doc_root) {
    // 确保文档根目录以 '/' 结尾
    if (!m_doc_root.empty() && m_doc_root.back() != '/') {
        m_doc_root += '/';
    }
}

HttpResponse StaticFileHandler::handle_static_file(const HttpRequest& request, const std::string& file_path) {
    std::string target_path;
    
    if (!file_path.empty()) {
        // 使用指定的文件路径
        target_path = file_path;
    } else {
        // 从请求路径构建文件路径
        target_path = build_file_path(request.path);
    }
    
    // 检查文件是否存在且可访问
    if (!is_file_accessible(target_path)) {
        return HttpResponse::make_error(404);
    }
    
    // 获取文件信息
    struct stat file_stat;
    if (stat(target_path.c_str(), &file_stat) != 0) {
        return HttpResponse::make_error(500);
    }
    
    // 检查是否为目录
    if (S_ISDIR(file_stat.st_mode)) {
        // 尝试查找 index.html
        std::string index_path = target_path;
        if (index_path.back() != '/') {
            index_path += '/';
        }
        index_path += "index.html";
        
        if (is_file_accessible(index_path)) {
            target_path = index_path;
            if (stat(target_path.c_str(), &file_stat) != 0) {
                return HttpResponse::make_error(500);
            }
        } else {
            return HttpResponse::make_error(403); // 禁止访问目录
        }
    }
    
    // 创建响应
    HttpResponse response;
    response.status_code = 200;
    response.status_text = "OK";
    response.file_path = target_path;
    response.file_size = file_stat.st_size;
    
    // 设置Content-Type
    std::string mime_type = get_mime_type(target_path);
    response.set_header("Content-Type", mime_type);
    
    // 设置缓存头
    response.set_header("Cache-Control", "public, max-age=3600");
    
    // 检查是否支持Keep-Alive
    auto connection_header = request.get_header("Connection");
    if (connection_header && (*connection_header == "keep-alive" || *connection_header == "Keep-Alive")) {
        response.set_header("Connection", "keep-alive");
    } else {
        response.set_header("Connection", "close");
    }
    
    return response;
}

std::string StaticFileHandler::get_mime_type(const std::string& file_path) {
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
    
    return "application/octet-stream";
}

bool StaticFileHandler::is_file_accessible(const std::string& file_path) const {
    struct stat file_stat;
    
    // 检查文件是否存在
    if (stat(file_path.c_str(), &file_stat) != 0) {
        return false;
    }
    
    // 检查是否可读
    if (access(file_path.c_str(), R_OK) != 0) {
        return false;
    }
    
    return true;
}

std::string StaticFileHandler::build_file_path(const std::string& request_path) const {
    std::string path = request_path;
    
    // 确保路径以 '/' 开头
    if (path.empty() || path[0] != '/') {
        path = "/" + path;
    }
    
    // 如果路径是根路径，默认返回 index.html
    if (path == "/") {
        path = "/index.html";
    }
    
    // 移除路径中的 ".." 和 "." 以防止目录遍历攻击
    std::filesystem::path clean_path = std::filesystem::path(path).lexically_normal();
    
    // 确保路径仍然以 '/' 开头（安全检查）
    std::string clean_path_str = clean_path.string();
    if (clean_path_str.empty() || clean_path_str[0] != '/') {
        return m_doc_root + "index.html"; // 默认返回首页
    }
    
    // 构建完整路径
    return m_doc_root + clean_path_str.substr(1); // 去掉开头的 '/'
}