// ThreadPool.cpp — 线程池实现
#include "../include/ThreadPool.h"
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <new>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

namespace dns {

// ==================== TaskQueue ====================

TaskQueue::TaskQueue(int capacity)
    : size_(0), capacity(capacity), shutdown_(false)
{
    head = tail = new TaskNode{nullptr, nullptr, nullptr};
    pthread_mutex_init(&lock, nullptr);
    pthread_cond_init(&cond, nullptr);
}

TaskQueue::~TaskQueue() {
    // 释放所有剩余节点
    TaskNode* cur = head;
    while (cur) {
        TaskNode* next = cur->next;
        delete cur;
        cur = next;
    }
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
}

bool TaskQueue::offer(TaskFunction task, void* arg) {
    pthread_mutex_lock(&lock);

    if (size_ >= capacity) {
        pthread_mutex_unlock(&lock);
        return false;
    }

    TaskNode* node = new TaskNode{task, arg, nullptr};
    tail->next = node;
    tail = node;
    ++size_;

    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
    return true;
}

TaskQueue::TaskResult TaskQueue::take(int timeoutSecs) {
    pthread_mutex_lock(&lock);
    TaskResult result{nullptr, nullptr};

    if (timeoutSecs < 0) {
        // 无限等待
        while (size_ == 0 && !shutdown_) {
            pthread_cond_wait(&cond, &lock);
        }
    } else if (timeoutSecs > 0) {
        struct timespec ts;
#ifdef _WIN32
        // MinGW: clock_gettime may not be available; use GetSystemTimeAsFileTime
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli;
        uli.LowPart  = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        // Convert 100ns intervals since 1601-01-01 to Unix epoch
        ts.tv_sec  = (time_t)((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
        ts.tv_nsec = 0;
#else
        clock_gettime(CLOCK_REALTIME, &ts);
#endif
        ts.tv_sec += timeoutSecs;
        while (size_ == 0 && !shutdown_) {
            int rc = pthread_cond_timedwait(&cond, &lock, &ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&lock);
                return result;  // 超时
            }
        }
    }
    // timeoutSecs == 0: 不等待，直接检查

    if (size_ == 0) {
        pthread_mutex_unlock(&lock);
        return result;  // 队列为空
    }

    // 出队
    TaskNode* node = head->next;
    result.task = node->task;
    result.arg  = node->arg;

    head->next = node->next;
    if (tail == node) tail = head;
    --size_;

    delete node;
    pthread_mutex_unlock(&lock);
    return result;
}

void TaskQueue::shutdown() {
    pthread_mutex_lock(&lock);
    shutdown_ = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

int TaskQueue::size() const { return size_; }
bool TaskQueue::empty() const { return size_ == 0; }
bool TaskQueue::full() const { return size_ >= capacity; }

// ==================== ThreadPool ====================

// 线程工作循环（pthread 入口，与头文件 friend 声明一致）
void* workerLoop(void* param) {
    ThreadPool::WorkerLoopParams* p = static_cast<ThreadPool::WorkerLoopParams*>(param);
    ThreadPool* pool  = p->pool;
    Worker*     worker = p->worker;
    TaskFunction firstTask = p->firstTask;
    void*       firstTaskArg = p->firstTaskArg;
    delete p;  // 参数由调用方堆分配，此处释放

    // 优先执行触发本次扩容的首个任务
    if (firstTask) {
        firstTask(firstTaskArg);
    }

    // 工作循环
    while (true) {
        int timeout = worker->isCore ? -1 : (int)pool->keepAliveSecs;
        TaskQueue::TaskResult result = pool->workQueue.take(timeout);

        if (result.task) {
            result.task(result.arg);
        } else {
            // 超时（非核心线程）或 shutdown 后队列空 → 退出
            break;
        }
    }

    // 退出：减少工作线程计数
    pthread_mutex_lock(&pool->poolLock);
    --pool->numWorking;
    int shouldTerminate = (pool->shutdown_ && pool->numWorking == 0);
    if (shouldTerminate) {
        pool->terminated = true;
    }
    pthread_mutex_unlock(&pool->poolLock);

    delete worker;  // Worker 在 createWorker 中堆分配
    return nullptr;
}

ThreadPool::ThreadPool(int corePoolSize, int maxPoolSize, time_t keepAliveSecs)
    : corePoolSize(corePoolSize),
      maxPoolSize(maxPoolSize),
      keepAliveSecs(keepAliveSecs),
      workQueue(50),          // 工作队列容量 50
      numWorking(0),
      shutdown_(false),
      terminated(false)
{
    pthread_mutex_init(&poolLock, nullptr);
}

ThreadPool::~ThreadPool() {
    if (!terminated) {
        shutdown();
        // 等待一段时间（让现有任务完成），然后强制释放
        // 生产环境建议 awaitTermination() 后再析构
    }
    pthread_mutex_destroy(&poolLock);
}

bool ThreadPool::execute(TaskFunction task, void* arg) {
    if (!task) return false;

    pthread_mutex_lock(&poolLock);

    if (shutdown_) {
        pthread_mutex_unlock(&poolLock);
        return false;
    }

    // 1. 核心线程未满 → 创建核心线程，直接交付任务（不经过队列）
    if ((int)workers.size() < corePoolSize) {
        Worker* w = createWorker(true, task, arg);
        workers.push_back(w);
        ++numWorking;
        pthread_mutex_unlock(&poolLock);
        return true;
    }

    pthread_mutex_unlock(&poolLock);

    // 2. 核心线程已满 → 尝试入队
    if (workQueue.offer(task, arg)) {
        return true;
    }

    // 3. 队列满 → 尝试创建非核心线程
    pthread_mutex_lock(&poolLock);

    if ((int)workers.size() < maxPoolSize) {
        Worker* w = createWorker(false, task, arg);
        workers.push_back(w);
        ++numWorking;
        pthread_mutex_unlock(&poolLock);
        return true;
    }

    // 4. 线程池满且队列满 → 拒绝
    // 相当于报错后catch，模拟拒绝策略
    pthread_mutex_unlock(&poolLock);
    fprintf(stderr, "[ThreadPool] task rejected: pool and queue full\n");
    return false;
}

Worker* ThreadPool::createWorker(bool isCore, TaskFunction firstTask, void* firstTaskArg) {
    Worker* worker = new Worker();
    worker->isCore = isCore;
    worker->spawnTimeSecs = time(nullptr);

    WorkerLoopParams* params = new WorkerLoopParams{this, worker, firstTask, firstTaskArg};

    pthread_create(&worker->thread, nullptr, workerLoop, params);
    pthread_detach(worker->thread);

    return worker;
}

void ThreadPool::shutdown() {
    pthread_mutex_lock(&poolLock);
    if (shutdown_) {
        pthread_mutex_unlock(&poolLock);
        return;
    }
    shutdown_ = true;
    pthread_mutex_unlock(&poolLock);

    workQueue.shutdown();  // 唤醒所有等待线程
}

void ThreadPool::awaitTermination() {
    while (true) {
        pthread_mutex_lock(&poolLock);
        if (terminated) {
            pthread_mutex_unlock(&poolLock);
            break;
        }
        pthread_mutex_unlock(&poolLock);
        // 简单轮询（生产环境可用条件变量）
#ifdef _WIN32
        Sleep(100);  // 100ms
#else
        struct timespec ts = {0, 100000000};  // 100ms
        nanosleep(&ts, nullptr);
#endif
    }
}

int ThreadPool::activeCount() const { return numWorking; }
int ThreadPool::poolSize()    const { return (int)workers.size(); }
int ThreadPool::queueSize()   const { return workQueue.size(); }
bool ThreadPool::isShutdown()   const { return shutdown_; }
bool ThreadPool::isTerminated() const { return terminated; }

} // namespace dns
