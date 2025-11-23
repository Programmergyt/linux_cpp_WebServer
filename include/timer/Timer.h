#ifndef TIMER_H
#define TIMER_H

#include <netinet/in.h>
#include <time.h>
#include <map>
#include <unordered_map>
#include <functional>
#include <mutex>
#include "../tools/Tools.h"

// 前向声明，避免循环依赖
struct client_data;

// 定时器对象
struct util_timer {
    time_t expire;                               // 定时器的绝对过期时间
    std::function<void(client_data*)> cb_func;   // 回调函数（超时后执行的操作）
    client_data* user_data;                      // 客户端数据指针
};

// 基于 multimap 的定时器管理器
class TimerManager 
{
public:
    /**
     * @brief 添加定时器
     * @param timer 需要添加的定时器指针
     */
    void add_timer(util_timer* timer);

    /**
     * @brief 调整定时器的过期时间
     * @param timer 需要调整的定时器指针
     * @param new_expire 新的过期时间
     */
    void adjust_timer(util_timer* timer, time_t new_expire);

    /**
     * @brief 删除定时器
     * @param timer 需要删除的定时器指针
     */
    void del_timer(util_timer* timer);

    /**
     * @brief 更新时间，并处理到期的定时器
     */
    void tick();

private:
    std::multimap<time_t, util_timer*> timers;
    std::unordered_map<util_timer*, std::multimap<time_t, util_timer*>::iterator> index;
    std::mutex timer_mutex; // 保护定时器操作的互斥锁
};

#endif // TIMER_H
