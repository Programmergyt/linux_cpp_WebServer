#ifndef HANDLER_H
#define HANDLER_H

#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "http/Router.h"
#include "sql/sql_connection_pool.h"
#include <string>

/**
 * @brief 处理静态文件请求的handler
 * 支持pdf、jpg、png、css、js、html等常用文件格式
 */
HttpResponse handle_static_file(const HttpRequest& req, RequestContext& ctx);

/**
 * @brief 处理用户注册请求的handler
 */
HttpResponse handle_register(const HttpRequest& req, RequestContext& ctx);

/**
 * @brief 处理用户登录请求的handler
 */
HttpResponse handle_login(const HttpRequest& req, RequestContext& ctx);

/**
 * @brief 处理简单GET请求返回JSON的handler
 */
HttpResponse handle_simple_json_get(const HttpRequest& req, RequestContext& ctx);

#endif // HANDLER_H
