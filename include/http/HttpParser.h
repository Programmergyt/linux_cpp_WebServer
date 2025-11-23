#pragma once
#include "HttpRequest.h"
#include <string>
#include <vector>
#include <fstream>
#include <optional>

class HttpParser {
public:
    enum class ParseResult {
        Complete,      // 成功解析一个完整请求
        Incomplete,    // 数据不完整，需要更多数据
        Error          // 解析出错
    };
    // 文件存在temp_upload_dir文件夹
    explicit HttpParser(std::string temp_upload_dir = "/tmp");

    ParseResult parse(std::vector<char>& buffer);
    void reset();
    const HttpRequest& get_request() const { return m_request; }

private:
    enum class ParseState {
        REQUEST_LINE,
        HEADERS,
        BODY, // 普通 application/json 或 x-www-form-urlencoded
        MULTIPART_BOUNDARY_SEARCH,
        MULTIPART_PART_HEADERS,
        MULTIPART_PART_DATA,
        DONE
    };

    ParseState m_state = ParseState::REQUEST_LINE;
    HttpRequest m_request;

    std::string m_temp_upload_dir;
    std::string m_boundary;

    // 当前处理的 multipart 部分信息
    std::string m_current_part_name;
    std::optional<UploadedFile> m_current_file;
    std::ofstream m_file_stream;

    // 内部工具函数
    bool parse_request_line(const std::string& line);
    bool parse_header_line(const std::string& line);
    bool extract_boundary(const std::string& content_type);
    void handle_form_urlencoded(const std::string& body);
    ParseResult handle_multipart(std::string_view& body_chunk);
};
