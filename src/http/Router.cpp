// Router.cpp

#include "http/Router.h"

/**
 * @brief 向路由表中添加一条路由规则
 * * @param method HTTP 方法 (GET, POST, etc.)
 * @param path 请求路径 (e.g., "/login")
 * @param handler 用于处理该请求的函数
 */
void Router::add_route(HttpMethod method, const std::string& path, HttpHandler handler) {
    // m_routes[method] 会访问（如果不存在则创建）对应方法下的路由表
    // 然后向该表中插入或更新路径和处理函数的映射
    m_routes[method][path] = handler;
}

/**
 * @brief 根据传入的请求，查找并执行对应的处理函数
 * * @param req 传入的 HttpRequest 对象
 * @param context 请求上下文，包含数据库连接池等资源
 * @return HttpResponse 处理函数返回的响应或一个错误响应
 */
HttpResponse Router::route_request(const HttpRequest& req, RequestContext& context) {
    // 1. 根据请求方法查找对应的路由表
    auto method_iter = m_routes.find(req.method);
    if (method_iter == m_routes.end()) {
        // 如果没有为该方法注册任何路由 (e.g., 没有POST路由)
        // 则返回 404 Not Found
        return HttpResponse::make_error(404);
    }

    // 2. 在该方法的路由表中，根据请求路径查找处理函数
    const auto& path_handlers = method_iter->second;
    auto handler_iter = path_handlers.find(req.path);

    if (handler_iter == path_handlers.end()) {
        // 如果该方法下没有为该路径注册处理函数
        // 则返回 404 Not Found
        return HttpResponse::make_error(404);
    }

    // 3. 找到处理函数，执行并返回其结果
    const HttpHandler& handler = handler_iter->second;
    return handler(req, context);
}