#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

static std::string ok_200_title       = "OK";
static std::string error_400_title    = "Bad Request";
static std::string error_400_form     = "Your request has bad syntax or is inherently impossible to satisfy.\n";
static std::string error_403_title    = "Forbidden";
static std::string error_403_form     = "You do not have permission to get file from this server.\n";
static std::string error_404_title    = "Not Found";
static std::string error_404_form     = "The requested file was not found on this server.\n";
static std::string error_500_title    = "Internal Error";
static std::string error_500_form     = "There was an unusual problem serving the requested file.\n";

// HttpResponse.h
class HttpResponse {
public:
    int status_code = 200;
    std::string status_text = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    // 用于发送文件
    std::string file_path;
    size_t file_size = 0;

    void set_header(const std::string& key, const std::string& value);
    // 构建标准错误响应
    static HttpResponse make_error(int code);
    // 使用流式接口（Fluent interface）来构建响应
    HttpResponse& with_status(int code, std::string_view text);
    HttpResponse& with_body(std::string_view b);
    HttpResponse& with_header(std::string_view key, std::string_view value);

    // 生成完整响应字符串（仅用于文本/JSON类型响应）
    std::string to_string() const;

};