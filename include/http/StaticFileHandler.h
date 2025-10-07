// StaticFileHandler.h
#pragma once
#include "HttpRequest.h"
#include "HttpResponse.h"
#include <string>
#include <unordered_map>

class StaticFileHandler {
public:
    // 构造函数接收文档根目录
    explicit StaticFileHandler(const std::string& doc_root);
    
    // 处理静态文件请求
    HttpResponse handle_static_file(const HttpRequest& request, const std::string& file_path = "");
    
    // 获取文件的MIME类型
    static std::string get_mime_type(const std::string& file_path);
    
private:
    std::string m_doc_root;
    
    // MIME类型映射表
    static const std::unordered_map<std::string, std::string> mime_types;
    
    // 检查文件是否存在且可读
    bool is_file_accessible(const std::string& file_path) const;
    
    // 构建完整的文件路径
    std::string build_file_path(const std::string& request_path) const;
};