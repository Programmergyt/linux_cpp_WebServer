#ifndef LOG_H
#define LOG_H

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include "block_queue.h"

class Log
{
public:
    int m_close_log; // 是否关闭日志

    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        return Log::get_instance()->async_write_log();
    }

    bool init(const char* file_name, int close_log,
              int log_buf_size = 8192,
              int split_lines = 5000000,
              int max_queue_size = 0);

    void write_log(int level, const char* format, ...);

    void flush();

private:
    Log();
    ~Log();

    void* async_write_log();
    void thread_func();
    void switch_log(const tm& my_tm);//日期更新时创建新log
    void write_single_log(const std::string& msg);

private:
    std::string m_dir_name;
    std::string m_log_name;
    int m_split_lines;
    int m_log_buf_size;
    long long m_count;//日志行数
    int m_today;
    FILE* m_fp;
    
    int m_flush_interval = 1;   // 每写多少条日志就 flush 一次，默认 10
    int m_unflushed = 0;         // 已经写入但未 flush 的日志条数
    std::thread m_thread;
    block_queue<std::string>* m_log_queue;
    std::mutex m_mutex;
    std::atomic<bool> m_exit;
    bool m_is_async;
};

// 宏定义

#define LOG_DEBUG(format, ...) \
    if(Log::get_instance()->m_close_log == 0) { \
        Log::get_instance()->write_log(0, format, ##__VA_ARGS__); \
    }

#define LOG_INFO(format, ...) \
    if(Log::get_instance()->m_close_log == 0) { \
        Log::get_instance()->write_log(1, format, ##__VA_ARGS__); \
    }

#define LOG_WARN(format, ...) \
    if(Log::get_instance()->m_close_log == 0) { \
        Log::get_instance()->write_log(2, format, ##__VA_ARGS__); \
    }

#define LOG_ERROR(format, ...) \
    if(Log::get_instance()->m_close_log == 0) { \
        Log::get_instance()->write_log(3, format, ##__VA_ARGS__); \
    }

#endif
