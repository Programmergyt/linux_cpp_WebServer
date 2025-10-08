// Router.h
#pragma once
#include <functional>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include <map>
#include <vector>
#include <regex>
#include "../sql/sql_connection_pool.h"

// 请求上下文，可以传递数据库连接池、配置等资源
class RequestContext {
public:
    connection_pool* db_pool;
    const char* doc_root;
};

// 定义一个处理请求的函数类型
using HttpHandler = std::function<HttpResponse(const HttpRequest&, RequestContext&)>;

// 路由规则结构体
struct RouteRule {
    std::string pattern;        // 原始路径模式
    std::regex regex_pattern;   // 编译后的正则表达式
    HttpHandler handler;        // 处理函数
    bool is_regex;             // 是否为正则表达式路径
    
    RouteRule(const std::string& path, HttpHandler h);
};

class Router {
public:
    void add_route(HttpMethod method, const std::string& path, HttpHandler handler);
    HttpResponse route_request(const HttpRequest& req, RequestContext& context);

private:
    // 使用 map，key 是 HttpMethod，值是该方法对应的路由规则列表
    std::map<HttpMethod, std::vector<RouteRule>> m_routes;
    
    // 检查路径是否包含正则表达式特殊字符
    bool is_regex_pattern(const std::string& path);
};