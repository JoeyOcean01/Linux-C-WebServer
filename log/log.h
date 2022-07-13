#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <iostream>
#include <string>
#include "block_queue.h"

/* 
 * 单例模式懒汉式，保证一个类仅有一个实例，并提供一个访问它的全局访问点
 * C++保证函数内local static对象在该函数被调用期间初始化
 * c++11以后，使用局部变量懒汉不用加锁，编译器保证local static对象安全
 */
class Log {
public:
    static Log* GetInstance() {
        static Log instance;
        return &instance;
    } 

    static void* FlushLogThread(void* arg) {
        Log::GetInstance()->asyncWriteLog();
    } 

    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char* file_name, int close_log, int log_buf_size = 8192,
              int split_lines = 5000000, int max_queue_size = 0);
    void writeLog(int level, const char* format, ...);
    void flush();

private:
    Log();
    ~Log();
    // 从阻塞队列中取出一个日志string，写入文件
    void* asyncWriteLog() {
        std::string single_log;
        while (m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
private:
    char m_dir_name[128];    // 路径名
    char m_log_name[128];    // log文件名
    int m_split_lines;       // 日志最大行数
    int m_log_buf_size;      // 日志缓冲区大小
    long long m_count;       // 日志行数纪录
    int m_today;             // 按天分类，记录当前时间是哪一天
    FILE* m_fp;              // 打开log的文件指针
    char* m_buf;
    BlockQueue<std::string>* m_log_queue; // 阻塞队列
    bool m_is_async;         // 是否同步标志位
    Locker m_mutex;
    int m_close_log;         // 关闭日志               
};

// 可变参数宏为其他程序提供调用方法
#define LOG_DEBUG(format, ...) if(m_close_log == 0) { Log::GetInstance()->writeLog(0, format, ##__VA_ARGS__); Log::GetInstance()->flush(); }
#define LOG_INFO(format, ...) if(m_close_log == 0) { Log::GetInstance()->writeLog(1, format, ##__VA_ARGS__); Log::GetInstance()->flush(); }
#define LOG_WARN(format, ...) if(m_close_log == 0) { Log::GetInstance()->writeLog(2, format, ##__VA_ARGS__); Log::GetInstance()->flush(); }
#define LOG_ERROR(format, ...) if(m_close_log == 0) { Log::GetInstance()->writeLog(3, format, ##__VA_ARGS__); Log::GetInstance()->flush(); }

#endif