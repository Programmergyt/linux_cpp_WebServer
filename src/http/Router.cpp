// Router.cpp

#include "http/Router.h"
#include <stdexcept>

/**
 * @brief RouteRule构造函数
 */
RouteRule::RouteRule(const std::string& path, HttpHandler h) 
    : pattern(path), handler(h) {
    // 检查是否包含正则表达式特殊字符
    is_regex = (path.find_first_of(".*+?^${}()|[]\\") != std::string::npos);
    
    if (is_regex) {
        try {
            // 编译正则表达式
            regex_pattern = std::regex(path);
        } catch (const std::regex_error& e) {
            // 如果正则表达式编译失败，将其作为普通字符串处理
            is_regex = false;
        }
    }
}

/**
 * @brief 向路由表中添加一条路由规则
 * @param method HTTP 方法 (GET, POST, etc.)
 * @param path 请求路径 (e.g., "/login" 或 "/api/user/\d+")
 * @param handler 用于处理该请求的函数
 */
void Router::add_route(HttpMethod method, const std::string& path, HttpHandler handler) {
    // 创建路由规则并添加到对应方法的规则列表中
    m_routes[method].emplace_back(path, handler);
}

/**
 * @brief 根据传入的请求，查找并执行对应的处理函数
 * @param req 传入的 HttpRequest 对象
 * @param context 请求上下文，包含数据库连接池等资源
 * @return HttpResponse 处理函数返回的响应或一个错误响应
 */
HttpResponse Router::route_request(const HttpRequest& req, RequestContext& context) {
    // 1. 根据请求方法查找对应的路由规则列表
    auto method_iter = m_routes.find(req.method);
    if (method_iter == m_routes.end()) {
        // 如果没有为该方法注册任何路由
        return HttpResponse::make_error(404);
    }

    // 2. 遍历该方法的所有路由规则，找到匹配的规则
    const auto& route_rules = method_iter->second;
    
    for (const auto& rule : route_rules) {
        bool matches = false;
        
        if (rule.is_regex) {
            // 使用正则表达式匹配
            std::smatch match_result;
            matches = std::regex_match(req.path, match_result, rule.regex_pattern);
        } else {
            // 使用精确字符串匹配
            matches = (req.path == rule.pattern);
        }
        
        if (matches) {
            // 找到匹配的路由，执行处理函数
            return rule.handler(req, context);
        }
    }

    // 3. 没有找到匹配的路由规则
    return HttpResponse::make_error(404);
}

/**
 * @brief 检查路径是否包含正则表达式特殊字符
 */
bool Router::is_regex_pattern(const std::string& path) {
    return path.find_first_of(".*+?^${}()|[]\\") != std::string::npos;
}