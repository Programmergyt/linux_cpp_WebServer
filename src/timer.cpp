#include "timer/timer.h"
#include <unistd.h>
#include <iostream>

void timer_manager::add_timer(util_timer* timer) {
    auto it = timers.insert({timer->expire, timer});
    index[timer] = it; // 保存索引
}

// 用法：tm.adjust_timer(t1, time(nullptr) + 4);定时器延长四秒
void timer_manager::adjust_timer(util_timer* timer, time_t new_expire) {
    auto it = index.find(timer);
    if (it == index.end()) return; // 不存在该定时器

    // 先从 multimap 删除旧位置
    timers.erase(it->second);

    // 更新过期时间并重新插入
    timer->expire = new_expire;
    auto new_it = timers.insert({timer->expire, timer});

    // 更新索引
    it->second = new_it;
}

void timer_manager::del_timer(util_timer* timer) {
    auto it = index.find(timer);
    if (it == index.end()) return;

    timers.erase(it->second); // 删除 multimap 节点
    index.erase(it);          // 删除索引
    delete timer;
}

void timer_manager::tick() {
    if (timers.empty()) return;

    time_t cur = time(nullptr);
    auto it = timers.begin();

    while (it != timers.end() && it->first <= cur) {
        util_timer* timer = it->second;

        if (timer->cb_func) {
            timer->cb_func(timer->user_data);
        }

        index.erase(timer);
        delete timer;
        it = timers.erase(it); // 返回下一个
    }
}
