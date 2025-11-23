#include "log/Log.h"
#include <ctime>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <sys/time.h>

// ---------------- 内部小工具：构建日志全路径 ----------------
static std::string make_log_fullname(const std::string& dir,
                                     const std::string& base,
                                     const tm& my_tm,
                                     long long count,
                                     int split_lines,
                                     bool new_day)
{
    // 日期前缀：YYYY_MM_DD_
    char date_prefix[32];
    snprintf(date_prefix, sizeof(date_prefix), "%d_%02d_%02d_",
             my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

    std::string full = dir;
    full += date_prefix;
    full += base;

    // 同一天按行切分：.1 .2 ...
    if (!new_day && split_lines > 0 && count > 0 && (count % split_lines) == 0) {
        char suffix[32];
        // 与常见实现一致：第一次达到 split_lines 时生成 ".1"
        long long idx = count / split_lines;
        snprintf(suffix, sizeof(suffix), ".%lld", idx);
        full += suffix;
    }
    return full;
}

// ---------------- 封装：日志更新切割 ----------------
void Log::switch_log(const tm& my_tm)
{
    bool new_day = (m_today != my_tm.tm_mday);
    if (new_day || (m_split_lines > 0 && (m_count % m_split_lines) == 0)) {
        fflush(m_fp);
        fclose(m_fp);

        if (new_day) {
            m_today = my_tm.tm_mday;
            m_count = 0; // 跨天重置计数
        }

        std::string full = make_log_fullname(m_dir_name, m_log_name, my_tm,
                                             m_count, m_split_lines, new_day);
        m_fp = fopen(full.c_str(), "a");
        if (!m_fp) {
            m_fp = stderr;
        }
    }
}

// ---------------- 封装：写入单条日志 ----------------
void Log::write_single_log(const std::string& msg)
{
    time_t t = time(nullptr);
    tm* sys_tm = localtime(&t);
    tm  my_tm  = *sys_tm;

    ++m_count;
    switch_log(my_tm);

    fputs(msg.c_str(), m_fp);

    // 计数到 flush_interval 才刷新一次
    if (++m_unflushed >= m_flush_interval) 
    {
        fflush(m_fp);
        m_unflushed = 0;
    }
}

Log::Log()
    : m_close_log(0), m_split_lines(5000000), m_log_buf_size(8192),
      m_count(0), m_today(0), m_fp(nullptr),
      m_log_queue(nullptr), m_exit(false), m_is_async(false) {}

Log::~Log()
{
    // 优先唤醒写线程安全退出
    if (m_is_async && m_log_queue) {
        m_log_queue->push("LOG_ENDS_NOW");
    }
    m_exit = true;
    if (m_is_async && m_thread.joinable()) {
        m_thread.join();
    }
    if (m_fp) {
        fflush(m_fp);
        fclose(m_fp);
        m_fp = nullptr;
    }
    delete m_log_queue;
    m_log_queue = nullptr;
}

bool Log::init(const char* file_name, int close_log,
               int log_buf_size, int split_lines, int max_queue_size)
{
    m_close_log    = close_log;
    m_log_buf_size = log_buf_size;
    m_split_lines  = (split_lines > 0 ? split_lines : 5000000);

    if (max_queue_size >= 1) {
        m_is_async   = true;
        m_log_queue  = new BlockQueue<std::string>(max_queue_size);
        m_thread     = std::thread(&Log::thread_func, this);
    }

    // 解析路径与基本名
    const char* p = strrchr(file_name, '/');
    if (!p) {
        m_dir_name = "";
        m_log_name = file_name;
    } else {
        m_log_name = std::string(p + 1);
        m_dir_name = std::string(file_name, p - file_name + 1); // 含末尾 '/'
    }

    // 初始打开：YYYY_MM_DD_ + m_log_name
    time_t t = time(nullptr);
    tm* sys_tm = localtime(&t);
    tm  my_tm  = *sys_tm;
    m_today    = my_tm.tm_mday;

    std::string full = make_log_fullname(m_dir_name, m_log_name, my_tm,
                                         /*count=*/0, m_split_lines,
                                         /*new_day=*/true);
    m_fp = fopen(full.c_str(), "a");
    if (!m_fp) {
        std::cerr << "Log file open error: " << full << std::endl;
        return false;
    }
    return true;
}

void Log::write_log(int level, const char* format, ...)
{
    // 组装一条日志文本（含时间与级别）
    struct timeval now{};
    gettimeofday(&now, nullptr);
    time_t t = time(nullptr);
    tm* sys_tm = localtime(&t);
    tm  my_tm  = *sys_tm;

    char s[16] = {0};
    switch (level) {
        case 0: strcpy(s, "[debug]:"); break;
        case 1: strcpy(s, "[info]:");  break;
        case 2: strcpy(s, "[warn]:");  break;
        case 3: strcpy(s, "[error]:"); break;
        default: strcpy(s, "[info]:"); break;
    }

    char buf[m_log_buf_size];
    va_list valst;
    va_start(valst, format);
    int n = snprintf(buf, 64, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec,
                     (long)now.tv_usec, s);
    int m = vsnprintf(buf + n, m_log_buf_size - n - 1, format, valst);
    va_end(valst);

    buf[n + m]     = '\n';
    buf[n + m + 1] = '\0';
    std::string log_str(buf);

    if (m_is_async) {
        // 异步：只入队，不触碰文件句柄
        m_log_queue->push(log_str);
    } else {
        // 同步：同一把锁内完成切割与写入，保证线程安全
        std::lock_guard<std::mutex> lk(m_mutex);

        // ——行计数与切割判断——
        // 先自增再判断，保持与常见实现一致（到 m_split_lines 时生成 ".1"）
        ++m_count;
        time_t t = time(nullptr);
        tm* sys_tm = localtime(&t);
        tm  my_tm  = *sys_tm;
        switch_log(my_tm); // 检查是否需要切割日志

        fputs(log_str.c_str(), m_fp);
        fflush(m_fp);
    }
}

void* Log::async_write_log()
{
    thread_func();
    return nullptr;
}

void Log::thread_func()
{
    std::string single_log;
    while (true) {
        m_log_queue->pop(single_log);

        if (single_log == "LOG_ENDS_NOW") 
        {
            // 消费剩余日志
            while (!m_log_queue->empty()) {
                std::string leftover;
                if (m_log_queue->pop(leftover, 10)) {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    write_single_log(leftover);
                }
            }
            break; // 退出循环
        }

        std::lock_guard<std::mutex> lk(m_mutex);
        write_single_log(single_log);
    }

    // 确保退出前 flush 一次
    if (m_fp) fflush(m_fp);
}


void Log::flush()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_fp) fflush(m_fp);
}
