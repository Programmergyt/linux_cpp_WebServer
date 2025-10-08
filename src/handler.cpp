#include "handler/handler.h"
#include "tools/tools.h"
#include "log/log.h"
#include "sql/sql_connection_pool.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <mysql/mysql.h>

/**
 * @brief 处理静态文件请求的handler
 * 支持pdf、jpg、png、css、js、html等常用文件格式
 */
HttpResponse handle_static_file(const HttpRequest& req, RequestContext& ctx) {
    LOG_DEBUG("--- Handler: handle_static_file called for path: %s ---", req.path.c_str());
    
    std::string request_path = req.path;
    
    // 特殊处理：根目录重定向到index.html
    if (request_path == "/" || request_path.empty()) {
        request_path = "/index.html";
        LOG_INFO("[INFO] Root path redirected to /index.html");
    }
    
    // 处理无扩展名的文件，尝试添加.html扩展名
    if (request_path.find('.') == std::string::npos && request_path != "/") {
        std::string html_path = request_path + ".html";
        std::string test_file_path = ctx.doc_root + html_path;
        if (std::filesystem::exists(test_file_path)) {
            request_path = html_path;
            LOG_INFO("[INFO] No extension file found as HTML: %s", request_path.c_str());
        }
    }
    
    // 构建完整的文件路径
    std::string file_path = ctx.doc_root + request_path;
    
    // 安全检查：防止路径遍历攻击
    std::string canonical_doc_root;
    std::string canonical_file_path;
    
    try {
        canonical_doc_root = std::filesystem::canonical(ctx.doc_root);
        canonical_file_path = std::filesystem::canonical(file_path);
        
        // 检查请求的文件是否在文档根目录下
        if (canonical_file_path.substr(0, canonical_doc_root.length()) != canonical_doc_root) {
            LOG_WARN("[SECURITY] Path traversal attempt detected: %s", req.path.c_str());
            return HttpResponse::make_error(403); // Forbidden
        }
    } catch (const std::filesystem::filesystem_error& e) {
        // 如果路径不存在或无法解析，返回404
        LOG_ERROR("[ERROR] Filesystem error: %s", e.what());
        return HttpResponse::make_error(404);
    }
    
    // 检查文件是否存在且是常规文件
    if (!std::filesystem::exists(canonical_file_path) || 
        !std::filesystem::is_regular_file(canonical_file_path)) {
        LOG_ERROR("[ERROR] File not found or not a regular file: %s", canonical_file_path.c_str());
        return HttpResponse::make_error(404);
    }
    
    // 检查文件权限（可读）
    std::ifstream file(canonical_file_path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("[ERROR] Cannot open file: %s", canonical_file_path.c_str());
        return HttpResponse::make_error(403); // Forbidden
    }
    
    // 获取文件大小
    std::error_code ec;
    auto file_size = std::filesystem::file_size(canonical_file_path, ec);
    if (ec) {
        LOG_ERROR("[ERROR] Cannot get file size: %s", ec.message().c_str());
        return HttpResponse::make_error(500);
    }
    
    // 获取MIME类型
    std::string mime_type = Tools::get_mime_type(canonical_file_path);
    
    // 创建响应
    HttpResponse response;
    response.status_code = 200;
    response.status_text = "OK";
    response.set_header("Content-Type", mime_type);
    response.set_header("Content-Length", std::to_string(file_size));
    
    // 添加缓存控制头
    if (mime_type.find("image/") == 0 || mime_type.find("font/") == 0 || 
        mime_type == "text/css" || mime_type == "application/javascript") {
        // 静态资源可以缓存较长时间
        response.set_header("Cache-Control", "public, max-age=3600"); // 1小时
    } else {
        // 其他文件缓存时间较短
        response.set_header("Cache-Control", "public, max-age=300"); // 5分钟
    }
    
    // 对于较小的文件（小于1MB），直接读取到内存
    if (file_size < 1024 * 1024) {
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        response.body = std::move(content);
        LOG_DEBUG("[SUCCESS] Small file loaded to memory: %llu bytes", (unsigned long long)file_size);
    } else {
        // 对于大文件，使用文件路径，让HttpConnection使用sendfile发送
        response.file_path = canonical_file_path;
        LOG_DEBUG("[SUCCESS] Large file will be sent via sendfile: %llu bytes", (unsigned long long)file_size);
    }
    
    file.close();
    return response;
}

/**
 * @brief 处理用户注册请求的handler
 */
HttpResponse handle_register(const HttpRequest& req, RequestContext& ctx) {
    LOG_DEBUG("--- Handler: handle_register called ---");
    
    // 检查请求方法
    if (req.method != HttpMethod::POST) {
        HttpResponse response;
        response.status_code = 405;
        response.status_text = "Method Not Allowed";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "只支持POST方法"})";
        return response;
    }
    
    // 解析表单数据
    std::string username = Tools::parse_form_field(req.raw_body, "username");
    std::string password = Tools::parse_form_field(req.raw_body, "password");
    std::string email = Tools::parse_form_field(req.raw_body, "email");
    
    LOG_INFO("[INFO] Register attempt: username=%s, email=%s", username.c_str(), email.c_str());
    
    // 检查必填字段
    if (username.empty() || password.empty()) {
        HttpResponse response;
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "用户名和密码不能为空"})";
        return response;
    }
    
    // 数据库操作
    if (ctx.db_pool == nullptr) {
        std::cout << "[ERROR] Database pool is null" << std::endl;
        HttpResponse response;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "数据库连接失败"})";
        return response;
    }
    
    MYSQL* mysql = nullptr;
    connectionRAII mysqlcon(&mysql, ctx.db_pool);
    
    if (mysql == nullptr) {
        LOG_ERROR("[ERROR] Failed to get database connection");
        HttpResponse response;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "数据库连接失败"})";
        return response;
    }
    
    // 构造SQL语句
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO users(username, password, email) VALUES('%s', '%s', '%s')",
             username.c_str(), password.c_str(), email.c_str());
    
    HttpResponse response;
    response.set_header("Content-Type", "application/json");
    
    if (mysql_query(mysql, sql)) {
        LOG_ERROR("[ERROR] MySQL query failed: %s", mysql_error(mysql));
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.body = R"({"status": "error", "msg": "注册失败"})";
    } else {
        LOG_INFO("[SUCCESS] User registered successfully: %s", username.c_str());
        response.status_code = 200;
        response.status_text = "OK";
        response.body = R"({"status": "ok", "msg": "注册成功"})";
    }
    
    return response;
}

/**
 * @brief 处理用户登录请求的handler
 */
HttpResponse handle_login(const HttpRequest& req, RequestContext& ctx) {
    LOG_DEBUG("--- Handler: handle_login called ---");
    
    // 检查请求方法
    if (req.method != HttpMethod::POST) {
        HttpResponse response;
        response.status_code = 405;
        response.status_text = "Method Not Allowed";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "只支持POST方法"})";
        return response;
    }
    
    // 解析表单数据
    std::string username = Tools::parse_form_field(req.raw_body, "username");
    std::string password = Tools::parse_form_field(req.raw_body, "password");
    
    LOG_INFO("Login attempt: username=%s", username.c_str());
    
    // 检查必填字段
    if (username.empty() || password.empty()) {
        HttpResponse response;
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "用户名和密码不能为空"})";
        return response;
    }
    
    // 数据库操作
    if (ctx.db_pool == nullptr) {
        std::cout << "[ERROR] Database pool is null" << std::endl;
        HttpResponse response;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "数据库连接失败"})";
        return response;
    }
    
    MYSQL* mysql = nullptr;
    connectionRAII mysqlcon(&mysql, ctx.db_pool);
    
    if (mysql == nullptr) {
        std::cout << "[ERROR] Failed to get database connection" << std::endl;
        HttpResponse response;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.set_header("Content-Type", "application/json");
        response.body = R"({"status": "error", "msg": "数据库连接失败"})";
        return response;
    }
    
    // 构造SQL查询语句
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id FROM users WHERE username='%s' AND password='%s'",
             username.c_str(), password.c_str());
    
    HttpResponse response;
    response.set_header("Content-Type", "application/json");
    
    if (mysql_query(mysql, sql)) {
        std::cout << "[ERROR] MySQL query failed: " << mysql_error(mysql) << std::endl;
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.body = R"({"status": "error", "msg": "登录失败"})";
    } else {
        MYSQL_RES* result = mysql_store_result(mysql);
        if (result == nullptr) {
            std::cout << "[ERROR] Failed to store result: " << mysql_error(mysql) << std::endl;
            response.status_code = 500;
            response.status_text = "Internal Server Error";
            response.body = R"({"status": "error", "msg": "登录失败"})";
        } else {
            my_ulonglong num_rows = mysql_num_rows(result);
            mysql_free_result(result);
            
            if (num_rows == 0) {
                LOG_INFO("[INFO] Login failed: invalid credentials for user: %s", username.c_str());
                response.status_code = 401;
                response.status_text = "Unauthorized";
                response.body = R"({"status": "error", "msg": "用户名或密码错误"})";
            } else {
                LOG_INFO("[SUCCESS] User logged in successfully: %s", username.c_str());
                response.status_code = 200;
                response.status_text = "OK";
                response.body = R"({"status": "ok", "msg": "登录成功"})";
            }
        }
    }
    
    return response;
}

/**
 * @brief 处理简单GET请求返回JSON的handler
 */
HttpResponse handle_simple_json_get(const HttpRequest& req, RequestContext& ctx) {
    LOG_DEBUG("--- Handler: handle_simple_json_get called ---");
    std::string json_response = R"({"message": "Hello from HttpConnection", "status": "success"})";
    return HttpResponse()
        .with_status(200, "OK")
        .with_header("Content-Type", "application/json")
        .with_body(json_response);
}