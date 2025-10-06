// HttpRequest.h (修改后)
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

// 新增：用于表示上传文件的结构体
struct UploadedFile {
    std::string filename;      // 客户端上传的原始文件名
    std::string content_type;  // 文件的 MIME 类型, 例如 'image/jpeg'
    std::string temp_file_path; // 文件内容被流式写入的服务器临时文件路径
    size_t      size = 0;        // 文件总大小
};

enum class HttpMethod { GET, POST, HEAD, UNKNOWN };

class HttpRequest {
public:
    // --- 以下部分保持不变 ---
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;

    std::optional<std::string_view> get_header(std::string_view key) const;

    // 用于存储 application/json 等类型的原始请求体
    std::string raw_body; 
    // --- 修改部分：用两个 map 替换原来的 std::string body ---
    // 用于存储普通的 application/x-www-form-urlencoded 或 multipart/form-data 中的文本字段
    std::unordered_map<std::string, std::string> form_fields;

    // 用于存储 multipart/form-data 中上传的文件
    std::unordered_map<std::string, UploadedFile> uploaded_files;

    // 工具方法：判断是否为 JSON 请求
    bool is_json() const;

    // 工具方法：判断是否为 multipart/form-data 上传
    bool is_multipart() const;

    // 工具方法：获取 boundary（仅当 multipart 时有效）
    std::string get_boundary() const;
};