#include "http/HttpRequest.h"
#include <algorithm>
#include <cctype>

// ---------------------
// 获取头部字段
// ---------------------
std::optional<std::string_view> HttpRequest::get_header(std::string_view key) const {
    // 不区分大小写匹配
    for (const auto& [k, v] : headers) {
        if (std::equal(k.begin(), k.end(), key.begin(), key.end(),
                       [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
            return std::string_view(v);
        }
    }
    return std::nullopt;
}

// ---------------------
// 判断是否为 JSON 请求
// ---------------------
bool HttpRequest::is_json() const {
    auto h = get_header("Content-Type");
    return h && h->find("application/json") != std::string_view::npos;
}

// ---------------------
// 判断是否为 multipart/form-data 请求
// ---------------------
bool HttpRequest::is_multipart() const {
    auto h = get_header("Content-Type");
    return h && h->find("multipart/form-data") != std::string_view::npos;
}

// ---------------------
// 提取 multipart boundary
// ---------------------
std::string HttpRequest::get_boundary() const {
    auto h = get_header("Content-Type");
    if (!h) return "";

    std::string_view value = *h;
    auto pos = value.find("boundary=");
    if (pos == std::string_view::npos) return "";

    std::string boundary(value.substr(pos + 9));
    // boundary 可能包含引号
    if (!boundary.empty() && boundary.front() == '"' && boundary.back() == '"') {
        boundary = boundary.substr(1, boundary.size() - 2);
    }
    return boundary;
}
