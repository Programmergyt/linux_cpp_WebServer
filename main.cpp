#include "./webserver/webserver.h"
#include "./config/config.h"

int main(int argc, char *argv[])
{
    // 需要修改的数据库信息,登录名,密码,库名
    string databaseURL = "localhost";
    string user = "root";
    string passwd = "gyt2003gyt";
    string databasename = "web_demo";

    // 解析命令行参数
    Config config;
    config.parse_arg(argc, argv);

    // 创建服务器对象并初始化
    WebServer server;
    server.init(config.PORT, databaseURL, user, passwd, databasename,
                 config.OPT_LINGER, config.TRIGMode, config.sql_num,
                  config.thread_num, config.close_log, config.actor_model); // 初始化服务器
    
    server.eventListen(); // 监听事件
    server.eventLoop(); // 事件循环
    return 0;
}