// Logger.cpp — 异步日志实现
#include "../include/Logger.h"
#include "../include/ThreadPool.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <new>

namespace dns {

// 静态成员定义
Logger::LogQueue*   Logger::queue      = nullptr;
ThreadPool*         Logger::pool        = nullptr;
bool                Logger::ready       = false;
int                 Logger::debugLevel_  = 1;  // 默认 INFO 级别

static const char* levelTags[] = {"ERR", "INF", "DBG"};

// ==================== LogQueue ====================

Logger::LogQueue::LogQueue(int capacity)
    : size_(0), capacity(capacity), shutdown_(false)
{
    head = tail = nullptr;
    pthread_mutex_init(&lock, nullptr);
    pthread_cond_init(&cond, nullptr);
}

Logger::LogQueue::~LogQueue() {
    // 释放剩余条目
    while (head) {
        LogEntry* next = head->next;
        free(head);
        head = next;
    }
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
}

void Logger::LogQueue::enqueue(LogEntry* entry) {
    if (!entry) return;

    pthread_mutex_lock(&lock);

    if (size_ >= capacity) {
        pthread_mutex_unlock(&lock);
        free(entry);  // 队满 → 丢弃
        return;
    }

    if (tail) {
        tail->next = entry;
    } else {
        head = entry;
    }
    tail = entry;
    entry->next = nullptr;
    ++size_;

    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
}

Logger::LogEntry* Logger::LogQueue::dequeue() {
    pthread_mutex_lock(&lock);

    while (size_ == 0 && !shutdown_) {
        pthread_cond_wait(&cond, &lock);
    }

    if (size_ == 0) {
        pthread_mutex_unlock(&lock);
        return nullptr;  // shutdown 且队列空
    }

    LogEntry* entry = head;
    head = entry->next;
    if (!head) tail = nullptr;
    --size_;

    pthread_mutex_unlock(&lock);
    return entry;
}

void Logger::LogQueue::shutdown() {
    pthread_mutex_lock(&lock);
    shutdown_ = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

int  Logger::LogQueue::size()  const { return size_; }
bool Logger::LogQueue::empty() const { return size_ == 0; }

// ==================== Logger ====================

void Logger::init(ThreadPool* pool, int queueCapacity) {
    if (!pool || queueCapacity <= 0) return;

    queue = new LogQueue(queueCapacity);
    pool  = pool;
    ready = true;

    // 提交消费者任务
    pool->execute(_M_consumerLoop, nullptr);
}

void Logger::shutdown() {
    if (!queue || !ready) return;
    queue->shutdown();

    // 释放残余条目
    while (true) {
        LogEntry* entry = queue->dequeue();
        if (!entry) break;
        free(entry);
    }

    ready = false;
    delete queue;
    queue = nullptr;
}

Logger::LogEntry* Logger::_M_createEntry(int level, const char* fmt, va_list args) {
    if (level > debugLevel_) return nullptr;

    LogEntry* entry = static_cast<LogEntry*>(malloc(sizeof(LogEntry)));
    if (!entry) return nullptr;
    entry->level = level;
    entry->next = nullptr;

    // 时间戳 + 级别前缀
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", t);

    int n = snprintf(entry->msg, MSG_MAX_LEN, "[%s][%s] ", timeBuf, levelTags[level]);
    if (n > 0 && n < MSG_MAX_LEN) {
        vsnprintf(entry->msg + n, MSG_MAX_LEN - n, fmt, args);
    }

    return entry;
}

void Logger::_M_consumerLoop(void* /*arg*/) {
    while (true) {
        LogEntry* entry = queue->dequeue();
        if (!entry) break;   // shutdown 且队列空

        // 串行输出（单消费者，无并发竞争）
        fprintf(stderr, "%s\n", entry->msg);
        fflush(stderr);
        free(entry);
    }
}

void Logger::_M_drain() {
    // 由 consumerLoop 执行，外部一般不需要调用
}

void Logger::_M_logSync(int level, const char* fmt, va_list args) {
    if (level > debugLevel_) return;

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", t);

    fprintf(stderr, "[%s][%s] ", timeBuf, levelTags[level]);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

// ---- 公共接口 ----

void Logger::error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (ready && queue) {
        LogEntry* entry = _M_createEntry(LOG_ERROR, fmt, args);
        if (entry) queue->enqueue(entry);
    } else {
        _M_logSync(LOG_ERROR, fmt, args);
    }
    va_end(args);
}

void Logger::info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (ready && queue) {
        LogEntry* entry = _M_createEntry(LOG_INFO, fmt, args);
        if (entry) queue->enqueue(entry);
    } else {
        _M_logSync(LOG_INFO, fmt, args);
    }
    va_end(args);
}

void Logger::debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (ready && queue) {
        LogEntry* entry = _M_createEntry(LOG_DEBUG, fmt, args);
        if (entry) queue->enqueue(entry);
    } else {
        _M_logSync(LOG_DEBUG, fmt, args);
    }
    va_end(args);
}

} // namespace dns
