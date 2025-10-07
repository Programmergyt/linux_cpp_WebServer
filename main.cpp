#include "webserver/webserver.h"
#include "config/config.h"

int main(int argc, char *argv[])
{
    // 需要修改的数据库信息,登录名,密码,库名
    // string databaseURL = "localhost";
    // string user = "root";
    // string passwd = "gyt2003gyt";
    // string databasename = "web_demo";
    // string doc_root = "./root";

    // // 解析命令行参数
    // Config config;
    // config.parse_arg(argc, argv);

    // // 创建服务器对象并初始化
    // WebServer server;
    // server.init(config.PORT, databaseURL, user, passwd, databasename,
    //              doc_root,config.sql_num,
    //               config.thread_num); // 初始化服务器
    
    // // 初始化各个组件
    // server.init_log();        // 初始化日志放在webserver外面好了。
    // server.init_sql_pool();   // 初始化数据库连接池
    // server.init_thread_pool(); // 初始化线程池
    // server.init_router();     // 初始化路由
    
    // server.event_listen(); // 监听事件
    // server.event_loop(); // 事件循环
    // return 0;
}
