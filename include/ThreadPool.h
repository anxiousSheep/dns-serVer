// ThreadPool.h — 手写线程池，模拟 Java ThreadPoolExecutor
// 核心/最大线程数、工作队列、keepalive 超时、公平条件变量
#pragma once

#include "../base/Vector.h"
#include <pthread.h>
#include <time.h>

namespace dns {

// 任务函数类型
using TaskFunction = void (*)(void* arg);

// ==================== 阻塞任务队列 ====================
class TaskQueue {
public:
    struct TaskNode {
        TaskFunction task;
        void*        arg;
        TaskNode*    next;
    };

    TaskQueue(int capacity);
    ~TaskQueue();

    // 生产者：添加任务（队满返回 false）
    bool offer(TaskFunction task, void* arg);

    // 消费者：取出任务
    // timeoutSecs: -1=无限等, 0=立即返回, >0=限时等
    // 返回 task=nullptr 表示超时且队列为空，或队列关闭且为空
    struct TaskResult {
        TaskFunction task;
        void*        arg;
    };
    TaskResult take(int timeoutSecs);

    // 关闭队列（唤醒所有等待线程）
    void shutdown();

    int  size() const;
    bool empty() const;
    bool full()  const;

private:
    TaskNode*     head;       // 哨兵头节点
    TaskNode*     tail;
    int           size_;
    int           capacity;
    bool          shutdown_;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
};

// ==================== 工作线程 ====================
class ThreadPool;

struct Worker {
    pthread_t thread;
    bool      isCore;
    time_t    spawnTimeSecs;

    Worker() : isCore(false), spawnTimeSecs(0) {}
};

// ==================== 线程池 ====================
class ThreadPool {
public:
    // corePoolSize: 核心线程数（常驻，阻塞等任务）
    // maxPoolSize:  最大线程数（队列满扩容上限）
    // keepAliveSecs: 非核心线程空闲超时（秒）
    ThreadPool(int corePoolSize, int maxPoolSize, time_t keepAliveSecs);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务（void* 版本，兼容 C 代码）
    // 返回 true=成功, false=线程池满且队列满（拒绝）
    bool execute(TaskFunction task, void* arg);

    // 关闭线程池（不阻塞，等待所有工作线程完成当前任务后退出）
    void shutdown();

    // 等待所有线程结束（调用前需先 shutdown）
    void awaitTermination();

    // 状态查询
    int  activeCount()  const;
    int  poolSize()     const;
    int  queueSize()    const;
    bool isShutdown()   const;
    bool isTerminated() const;

private:
    friend void* workerLoop(void* arg);

    struct WorkerLoopParams {
        ThreadPool*   pool;
        Worker*       worker;
        TaskFunction  firstTask;
        void*         firstTaskArg;
    };

    // 线程池状态
    int           corePoolSize;
    int           maxPoolSize;
    time_t        keepAliveSecs;
    TaskQueue     workQueue;
    Vector<Worker*> workers;
    int           numWorking;

    bool          shutdown_;
    bool          terminated;

    pthread_mutex_t poolLock;

    // 内部方法
    Worker* createWorker(bool isCore, TaskFunction firstTask, void* firstTaskArg);
    void    workerExited();
};

} // namespace dns
