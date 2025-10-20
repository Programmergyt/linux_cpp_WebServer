#include "timer/Timer.h"
#include <unistd.h>
#include <iostream>
#include <mutex>
#include <vector>

void TimerManager::add_timer(util_timer* timer) {
    std::lock_guard<std::mutex> lock(timer_mutex);
    auto it = timers.insert({timer->expire, timer});
    index[timer] = it; // 保存索引
}

void TimerManager::adjust_timer(util_timer* timer, time_t new_expire) {
    std::lock_guard<std::mutex> lock(timer_mutex);
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

void TimerManager::del_timer(util_timer* timer) {
    std::lock_guard<std::mutex> lock(timer_mutex);
    auto it = index.find(timer);
    if (it == index.end()) return; // 定时器已经被删除

    timers.erase(it->second); // 删除 multimap 节点
    index.erase(it);          // 删除索引
    delete timer;
}

void TimerManager::tick() {
    std::vector<util_timer*> expired_timers;
    
    // 在锁内收集过期的定时器
    {
        std::lock_guard<std::mutex> lock(timer_mutex);
        if (timers.empty()) return;

        time_t cur = time(nullptr);
        auto it = timers.begin();

        while (it != timers.end() && it->first <= cur) {
            util_timer* timer = it->second;
            expired_timers.push_back(timer);
            
            // 从索引中删除
            index.erase(timer);
            it = timers.erase(it); // 删除并获取下一个迭代器
        }
    }
    
    // 在锁外执行回调函数并删除定时器
    for (util_timer* timer : expired_timers) {
        if (timer->cb_func) {
            timer->cb_func(timer->user_data);
        }
        delete timer;
    }
}
