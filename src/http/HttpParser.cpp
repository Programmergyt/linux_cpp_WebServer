// HttpParser.cpp (C++17 兼容版)

#include "http/HttpParser.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <string_view>
#include <filesystem>
#include <utility>
#include <ctime> // For std::time

// --- 辅助函数 (保持不变) ---
static std::string url_decode(const std::string& encoded) {
    // ... 实现和之前一样 ...
    std::string res;
    res.reserve(encoded.length());
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '+') {
            res += ' ';
        } else if (encoded[i] == '%' && i + 2 < encoded.length()) {
            try {
                std::string hex = encoded.substr(i + 1, 2);
                res += static_cast<char>(std::stoi(hex, nullptr, 16));
                i += 2;
            } catch (...) {
                res += encoded[i];
            }
        } else {
            res += encoded[i];
        }
    }
    return res;
}
static std::vector<std::string> split(const std::string& s, char delimiter) {
    // ... 实现和之前一样 ...
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// --- HttpParser 类成员函数实现 (保持不变) ---
HttpParser::HttpParser(std::string temp_upload_dir)
    : m_temp_upload_dir(std::move(temp_upload_dir)) {
    if (!m_temp_upload_dir.empty()) {
        std::filesystem::create_directories(m_temp_upload_dir);
    }
    reset();
}
void HttpParser::reset() {
    m_state = ParseState::REQUEST_LINE;
    m_request = HttpRequest();
    m_boundary.clear();
    m_current_part_name.clear();
    m_current_file.reset();
    if (m_file_stream.is_open()) {
        m_file_stream.close();
    }
}
bool HttpParser::parse_request_line(const std::string& line) {
    // ... 实现和之前一样 ...
    std::istringstream iss(line);
    std::string method_str, path, version;
    if (!(iss >> method_str >> path >> version)) {
        return false;
    }
    if (method_str == "GET") m_request.method = HttpMethod::GET;
    else if (method_str == "POST") m_request.method = HttpMethod::POST;
    else if (method_str == "HEAD") m_request.method = HttpMethod::HEAD;
    else m_request.method = HttpMethod::UNKNOWN;
    m_request.path = path;
    m_request.version = version;
    return true;
}
bool HttpParser::parse_header_line(const std::string& line) {
    // ... 实现和之前一样 ...
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        return false;
    }
    std::string key = line.substr(0, colon_pos);
    std::string value = line.substr(colon_pos + 1);
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    m_request.headers[key] = value;
    return true;
}
void HttpParser::handle_form_urlencoded(const std::string& body) {
    // ... 实现和之前一样 ...
    std::vector<std::string> pairs = split(body, '&');
    for (const auto& pair : pairs) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = url_decode(pair.substr(0, eq_pos));
            std::string value = url_decode(pair.substr(eq_pos + 1));
            m_request.form_fields[key] = value;
        }
    }
}
// 主要解析逻辑，和之前一样
HttpParser::ParseResult HttpParser::parse(std::vector<char>& buffer) {
    // ... 这部分代码完全不需要修改，和之前一样 ...
    std::string_view data(buffer.data(), buffer.size());
    size_t processed_bytes = 0;
    while (processed_bytes < data.size()) {
        switch (m_state) {
            case ParseState::REQUEST_LINE:
            case ParseState::HEADERS: {
                size_t crlf_pos = data.find("\r\n", processed_bytes);
                if (crlf_pos == std::string_view::npos) return ParseResult::Incomplete;
                std::string line = std::string(data.substr(processed_bytes, crlf_pos - processed_bytes));
                processed_bytes = crlf_pos + 2;
                if (m_state == ParseState::REQUEST_LINE) {
                    if (!parse_request_line(line)) return ParseResult::Error;
                    m_state = ParseState::HEADERS;
                } else {
                    if (line.empty()) {
                        if (m_request.is_multipart()) {
                            m_boundary = "--" + m_request.get_boundary();
                            if (m_boundary == "--") return ParseResult::Error;
                            m_state = ParseState::MULTIPART_BOUNDARY_SEARCH;
                        } else if (m_request.get_header("Content-Length")) {
                            m_state = ParseState::BODY;
                        } else {
                            m_state = ParseState::DONE;
                        }
                    } else {
                        if (!parse_header_line(line)) return ParseResult::Error;
                    }
                }
                break;
            }
            case ParseState::BODY: {
                auto content_length_str = m_request.get_header("Content-Length");
                if (!content_length_str) return ParseResult::Error;
                size_t content_length = 0;
                try {
                    content_length = std::stoul(std::string(*content_length_str));
                } catch(...) { return ParseResult::Error; }
                size_t body_received = m_request.raw_body.size();
                size_t bytes_to_read = std::min(data.size() - processed_bytes, content_length - body_received);
                m_request.raw_body.append(data.substr(processed_bytes, bytes_to_read));
                processed_bytes += bytes_to_read;
                if (m_request.raw_body.size() == content_length) {
                    auto content_type = m_request.get_header("Content-Type");
                    if (content_type && content_type->find("application/x-www-form-urlencoded") != std::string::npos) {
                        handle_form_urlencoded(m_request.raw_body);
                    }
                    m_state = ParseState::DONE;
                } else if (m_request.raw_body.size() > content_length) {
                    return ParseResult::Error;
                }
                break;
            }
            case ParseState::MULTIPART_BOUNDARY_SEARCH:
            case ParseState::MULTIPART_PART_HEADERS:
            case ParseState::MULTIPART_PART_DATA: {
                std::string_view chunk = data.substr(processed_bytes);
                ParseResult result = handle_multipart(chunk);
                if (result == ParseResult::Error) return result;
                processed_bytes = data.size() - chunk.size();
                if (result == ParseResult::Incomplete) {
                    buffer.erase(buffer.begin(), buffer.begin() + processed_bytes);
                    return ParseResult::Incomplete;
                }
                break;
            }
            case ParseState::DONE:
                buffer.erase(buffer.begin(), buffer.begin() + processed_bytes);
                return ParseResult::Complete;
        }
        if (m_state == ParseState::DONE) {
            buffer.erase(buffer.begin(), buffer.begin() + processed_bytes);
            return ParseResult::Complete;
        }
    }
    buffer.erase(buffer.begin(), buffer.begin() + processed_bytes);
    return ParseResult::Incomplete;
}


// --- handle_multipart 函数是修改的重点 ---
HttpParser::ParseResult HttpParser::handle_multipart(std::string_view& body_chunk) {
    while (!body_chunk.empty()) {
        if (m_state == ParseState::MULTIPART_BOUNDARY_SEARCH) {
            // C++20: if (body_chunk.starts_with("\r\n"))
            if (body_chunk.size() >= 2 && body_chunk.find("\r\n") == 0) { // C++17 兼容写法
                body_chunk.remove_prefix(2);
            }
            
            size_t boundary_pos = body_chunk.find(m_boundary);
            if (boundary_pos == std::string_view::npos) return ParseResult::Incomplete;

            body_chunk.remove_prefix(boundary_pos + m_boundary.length());

            // C++20: if (body_chunk.starts_with("--"))
            if (body_chunk.size() >= 2 && body_chunk.find("--") == 0) { // C++17 兼容写法
                m_state = ParseState::DONE;
                body_chunk.remove_prefix(2);
                // C++20: if (body_chunk.starts_with("\r\n"))
                if (body_chunk.size() >= 2 && body_chunk.find("\r\n") == 0) { // C++17 兼容写法
                    body_chunk.remove_prefix(2);
                }
                return ParseResult::Complete;
            // C++20: } else if (body_chunk.starts_with("\r\n")) {
            } else if (body_chunk.size() >= 2 && body_chunk.find("\r\n") == 0) { // C++17 兼容写法
                body_chunk.remove_prefix(2);
                m_state = ParseState::MULTIPART_PART_HEADERS;
            } else {
                return ParseResult::Error;
            }

        } else if (m_state == ParseState::MULTIPART_PART_HEADERS) {
            // ... 这部分逻辑不需要修改 ...
            size_t headers_end_pos = body_chunk.find("\r\n\r\n");
            if (headers_end_pos == std::string_view::npos) return ParseResult::Incomplete;
            
            std::string headers_str = std::string(body_chunk.substr(0, headers_end_pos));
            body_chunk.remove_prefix(headers_end_pos + 4);
            
            std::istringstream iss(headers_str);
            std::string header_line;
            std::string content_type = "text/plain";
            m_current_file.reset();
            m_current_part_name.clear();

            while (std::getline(iss, header_line) && !header_line.empty()) {
                if (header_line.back() == '\r') header_line.pop_back();

                if (header_line.rfind("Content-Disposition:", 0) == 0) {
                    size_t name_pos = header_line.find("name=\"");
                    if (name_pos != std::string::npos) {
                        name_pos += 6;
                        size_t name_end_pos = header_line.find("\"", name_pos);
                        m_current_part_name = header_line.substr(name_pos, name_end_pos - name_pos);
                    }
                    
                    size_t filename_pos = header_line.find("filename=\"");
                    if (filename_pos != std::string::npos) {
                        filename_pos += 10;
                        size_t filename_end_pos = header_line.find("\"", filename_pos);
                        std::string filename = header_line.substr(filename_pos, filename_end_pos - filename_pos);

                        m_current_file.emplace();
                        m_current_file->filename = filename;
                        std::string temp_path = m_temp_upload_dir + "/" + std::to_string(std::time(nullptr)) + "_" + filename;
                        m_current_file->temp_file_path = temp_path;

                        m_file_stream.open(temp_path, std::ios::binary);
                        if (!m_file_stream.is_open()) return ParseResult::Error;
                    }
                }
                else if (header_line.rfind("Content-Type:", 0) == 0) {
                     content_type = header_line.substr(header_line.find(":") + 2);
                }
            }

            if (m_current_file) {
                m_current_file->content_type = content_type;
            }
            m_state = ParseState::MULTIPART_PART_DATA;

        } else if (m_state == ParseState::MULTIPART_PART_DATA) {
            // ... 这部分逻辑不需要修改 ...
            std::string boundary_with_crlf = "\r\n" + m_boundary;
            size_t boundary_pos = body_chunk.find(boundary_with_crlf);
            
            size_t data_size = (boundary_pos == std::string_view::npos) ? body_chunk.size() : boundary_pos;
            std::string_view part_data = body_chunk.substr(0, data_size);
            
            if (m_current_file) {
                m_file_stream.write(part_data.data(), part_data.size());
                m_current_file->size += part_data.size();
            } else if (!m_current_part_name.empty()) {
                m_request.form_fields[m_current_part_name].append(part_data);
            }
            
            body_chunk.remove_prefix(data_size);

            if (boundary_pos != std::string_view::npos) {
                if (m_current_file) {
                    m_file_stream.close();
                    m_request.uploaded_files[m_current_part_name] = *m_current_file;
                    m_current_file.reset();
                }
                m_state = ParseState::MULTIPART_BOUNDARY_SEARCH;
            } else {
                return ParseResult::Incomplete;
            }
        }
    }

    return (m_state == ParseState::DONE) ? ParseResult::Complete : ParseResult::Incomplete;
}