#ifndef TIMER_H
#define TIMER_H

#include <netinet/in.h>
#include <time.h>
#include <map>
#include <unordered_map>
#include <functional>
#include <mutex>
#include "../tools/tools.h"

// 前向声明，避免循环依赖
struct client_data;

// 定时器对象
struct util_timer {
    time_t expire;                               // 定时器的绝对过期时间
    std::function<void(client_data*)> cb_func;   // 回调函数（超时后执行的操作）
    client_data* user_data;                      // 客户端数据指针
};

// 定时器管理器，基于 multimap 管理定时器
class timer_manager 
{
public:
    // 添加一个定时器
    void add_timer(util_timer* timer);

    // 调整定时器（例如延长时间）
    void adjust_timer(util_timer* timer, time_t new_expire);

    // 删除定时器
    void del_timer(util_timer* timer);

    // 执行过期定时器（tick）
    void tick();

private:
    // multimap 自动按 key (过期时间) 升序排序
    std::multimap<time_t, util_timer*> timers;
    std::unordered_map<util_timer*, std::multimap<time_t, util_timer*>::iterator> index;
    std::mutex timer_mutex; // 保护定时器操作的互斥锁
};

#endif // TIMER_H
