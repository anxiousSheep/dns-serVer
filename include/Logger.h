// Logger.h — 异步日志系统
// 多生产者单消费者模型，队满丢弃策略（永不阻塞业务线程）
#pragma once

#include <pthread.h>
#include <cstdarg>

namespace dns {

class ThreadPool;  // 前置声明

// ==================== 日志级别 ====================
enum LogLevel {
    LOG_ERROR = 0,
    LOG_INFO  = 1,
    LOG_DEBUG = 2,
};

// ==================== 异步日志 ====================
class Logger {
public:
    static const int MSG_MAX_LEN = 512;

    // 链表节点（日志消息）
    struct LogEntry {
        int       level;
        char      msg[MSG_MAX_LEN];
        LogEntry* next;
    };

    // 有界阻塞队列
    class LogQueue {
    public:
        explicit LogQueue(int capacity);
        ~LogQueue();

        void enqueue(LogEntry* entry);  // 满则丢弃（不阻塞）
        LogEntry* dequeue();            // 空则阻塞
        void shutdown();
        int  size() const;
        bool empty() const;

    private:
        LogEntry* head;
        LogEntry* tail;
        int       size_;
        int       capacity;
        bool      shutdown_;
        pthread_mutex_t lock;
        pthread_cond_t  cond;
    };

    // ==================== Logger 公开接口 ====================

    // 初始化：向线程池提交一个消费者任务
    static void init(ThreadPool* pool, int queueCapacity);

    // 关闭：优雅排空队列后退出消费者
    static void shutdown();

    // 格式化日志（异步入队，若未初始化则回退同步输出）
    static void error(const char* fmt, ...);
    static void info(const char* fmt, ...);
    static void debug(const char* fmt, ...);

    // 设置调试级别
    static void setDebugLevel(int level) { debugLevel_ = level; }
    static int  debugLevel()             { return debugLevel_; }

private:
    static LogQueue*   queue;
    static ThreadPool* pool;
    static bool        ready;
    static int         debugLevel_;

    // 格式化一条日志到 entry
    static LogEntry* createEntry(int level, const char* fmt, va_list args);

    // 同步日志（异步未初始化时的 fallback）
    static void logSync(int level, const char* fmt, va_list args);

    // 消费者循环（线程池任务函数）
    static void consumerLoop(void* arg);

    // 从队列取消息并串行输出
    static void drain();
};

} // namespace dns
