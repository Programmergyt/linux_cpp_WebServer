#include "config/config.h"
#include <iostream>
#include <unistd.h>
using namespace std;
Config::Config(){
    //端口号,默认8080
    PORT = 8080;

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量,默认8
    thread_num = 8;

    //关闭日志,默认不关闭
    close_log = 0;
}

void Config::parse_arg(int argc, char*argv[])
{
    int opt;
    const char *str = "p:s:t:c:";
    // getopt 会根据 str = "p:l:m:o:s:t:c:a:" 来匹配参数。
    // 含 : 的选项表示必须跟一个值比如 -p 8080，opt = 'p'，optarg = "8080"
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}