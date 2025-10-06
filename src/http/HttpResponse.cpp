#include "http/HttpResponse.h"
#include <sstream>
#include <filesystem>

void HttpResponse::set_header(const std::string& key, const std::string& value) {
    headers[key] = value;
}

HttpResponse& HttpResponse::with_status(int code, std::string_view text) {
    status_code = code;
    status_text = text;
    return *this;
}

HttpResponse& HttpResponse::with_body(std::string_view b) {
    body = b;
    headers["Content-Length"] = std::to_string(body.size());
    return *this;
}

HttpResponse& HttpResponse::with_header(std::string_view key, std::string_view value) {
    headers[std::string(key)] = std::string(value);
    return *this;
}

// ---------------------
// 构建完整 HTTP 响应文本
// ---------------------
std::string HttpResponse::to_string() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    for (const auto& [k, v] : headers) {
        oss << k << ": " << v << "\r\n";
    }
    oss << "\r\n";
    oss << body;
    return oss.str();
}

// ---------------------
// 构建标准错误响应
// ---------------------
HttpResponse HttpResponse::make_error(int code) {
    HttpResponse resp;

    switch (code) {
        case 400:
            resp.with_status(400, error_400_title).with_body(error_400_form);
            break;
        case 403:
            resp.with_status(403, error_403_title).with_body(error_403_form);
            break;
        case 404:
            resp.with_status(404, error_404_title).with_body(error_404_form);
            break;
        case 500:
        default:
            resp.with_status(500, error_500_title).with_body(error_500_form);
            break;
    }

    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Connection", "close");
    return resp;
}
