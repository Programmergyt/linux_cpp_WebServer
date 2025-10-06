// Router.h
#pragma once
#include <functional>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include <map>
#include "../sql/sql_connection_pool.h"

// 请求上下文，可以传递数据库连接池、配置等资源
class RequestContext {
public:
    connection_pool* db_pool;
    const char* doc_root;
};

// 定义一个处理请求的函数类型
using HttpHandler = std::function<HttpResponse(const HttpRequest&, RequestContext&)>;

class Router {
public:
    void add_route(HttpMethod method, const std::string& path, HttpHandler handler);
    HttpResponse route_request(const HttpRequest& req, RequestContext& context);

private:
    // 使用 map，key 是 HttpMethod，值是该方法对应的路由表
    std::map<HttpMethod, std::unordered_map<std::string, HttpHandler>> m_routes;
};