// stress_test.cpp — 组件级压力测试
// 直测各手写组件的上限：扩容 / 缓存淘汰 / 线程池吞吐与拒绝 / 日志洪泛丢包
#include "Vector.h"
#include "DnsString.h"
#include "LinkedHashMap.h"
#include "ThreadPool.h"
#include "Logger.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#define msleep(ms) Sleep(ms)
#else
#include <unistd.h>
#define msleep(ms) usleep((ms) * 1000)
#endif

using Clock = std::chrono::steady_clock;

static long long elapsedMs(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

// ---- ThreadPool 任务计数（无锁原子） ----
static std::atomic<int> g_counter{0};
static void incTask(void* /*arg*/) { ++g_counter; }

struct SlowParams { int sleepMs; };
static void slowTask(void* arg) {
    SlowParams* p = static_cast<SlowParams*>(arg);
    msleep(p->sleepMs);
    delete p;
}

// ==================== Vector ====================
static void stressVector() {
    printf("=== 1. Vector: push_back 1,000,000 扩容 ===\n");
    dns::Vector<int> v;
    auto t0 = Clock::now();
    for (int i = 0; i < 1000000; ++i) v.push_back(i);
    auto t1 = Clock::now();

    printf("  size     = %zu\n", v.size());
    printf("  capacity = %zu (预期 2 的幂 = 1048576)\n", v.capacity());
    printf("  耗时     = %lld ms\n", elapsedMs(t0, t1));
    printf("  front/back = %d / %d\n", v.front(), v.back());
}

// ==================== String ====================
static void stressString() {
    printf("\n=== 2. String: SSO 边界 + 长串 append ===\n");

    // SSO 边界：14 字节在栈内，16 字节触发堆分配
    dns::String s14("12345678901234");   // 14 字节
    dns::String s15("123456789012345");  // 15 字节（仍 SSO）
    dns::String s16("1234567890123456"); // 16 字节（越 SSO 边界）
    printf("  s14 len=%zu c_str=%s\n", s14.length(), s14.c_str());
    printf("  s15 len=%zu c_str=%s\n", s15.length(), s15.c_str());
    printf("  s16 len=%zu c_str=%s\n", s16.length(), s16.c_str());

    // 100KB 长串拼接
    const int N = 100000;
    dns::String big;
    auto t0 = Clock::now();
    char unit[2] = {'x', '\0'};
    for (int i = 0; i < N; ++i) big.append(unit, 1);
    auto t1 = Clock::now();
    printf("  100KB append: len=%zu, 耗时=%lld ms\n", big.length(), elapsedMs(t0, t1));
}

// ==================== LinkedHashMap ====================
static void stressLinkedHashMap() {
    printf("\n=== 3. LinkedHashMap: LRU 淘汰 + get 吞吐 ===\n");
    const size_t CAP = 256;

    // 淘汰测试：插入 100,000 个 key，容量 256 → 持续淘汰最旧，size 恒为 256
    dns::LinkedHashMap<int, int> map(CAP, true);  // accessOrder=true → LRU
    auto t0 = Clock::now();
    for (int i = 0; i < 100000; ++i) map.put(i, i * 2);
    auto t1 = Clock::now();
    printf("  插入 100k key 后 size=%zu (预期 == %zu)\n", map.size(), CAP);
    printf("  插入耗时 = %lld ms\n", elapsedMs(t0, t1));

    // get 吞吐：填满容量（key 0..255 全部命中），100w 次 get
    dns::LinkedHashMap<int, int> m2(CAP, true);
    for (int i = 0; i < (int)CAP; ++i) m2.put(i, i);
    long long hit = 0;
    t0 = Clock::now();
    for (int i = 0; i < 1000000; ++i) {
        if (m2.get(i % (int)CAP)) ++hit;
    }
    t1 = Clock::now();
    long long ms = elapsedMs(t0, t1);
    double qps = ms > 0 ? (1000000.0 * 1000.0 / ms) : 0.0;
    printf("  100w 次 get(全命中): 命中=%lld, 耗时=%lld ms, 吞吐=%.1f 万/s\n",
           hit, ms, qps / 10000.0);
}

// ==================== ThreadPool ====================
static void stressThreadPool() {
    printf("\n=== 4. ThreadPool: 吞吐 + 拒绝 ===\n");
    {
        // 吞吐：50k 任务，core=4 max=8（池满会拒绝，统计实际接受数）
        dns::ThreadPool pool(4, 8, 60);
        g_counter = 0;
        const int TOTAL = 50000;
        int accepted = 0;
        auto t0 = Clock::now();
        for (int i = 0; i < TOTAL; ++i) {
            if (pool.execute(incTask, nullptr)) ++accepted;
        }
        // 等待已接受的任务全部执行完
        while (g_counter.load() < accepted) msleep(1);
        auto t1 = Clock::now();
        long long ms = elapsedMs(t0, t1);
        printf("  提交 %d 任务: 接受=%d, 拒绝=%d, 全部执行=%d\n",
               TOTAL, accepted, TOTAL - accepted, g_counter.load());
        printf("  耗时=%lld ms, 吞吐=%.2f w/s\n",
               ms, ms > 0 ? (accepted / 10000.0) / (ms / 1000.0) : 0.0);
        pool.shutdown();
    }
    {
        // 拒绝：core=1 max=1，队列 50 → 慢任务填满后 execute() 返回 false
        dns::ThreadPool pool(1, 1, 5);
        int accepted = 0, rejected = 0;
        for (int i = 0; i < 200; ++i) {
            SlowParams* p = new SlowParams{5};
            if (pool.execute(slowTask, p)) ++accepted;
            else { delete p; ++rejected; }
        }
        printf("  拒绝策略: 慢任务 200 个 → 接受=%d, 拒绝=%d (预期拒绝>0)\n",
               accepted, rejected);
        msleep(3000);  // 等慢任务跑完
        pool.shutdown();
    }
}

// ==================== Logger ====================
static void stressLogger() {
    printf("\n=== 5. Logger: 洪泛丢包（不阻塞） ===\n");
    dns::ThreadPool pool(1, 1, 5);      // 单消费者，故意慢
    dns::Logger::init(&pool, 128);       // 小队列，快速填满

    const int TOTAL = 50000;
    auto t0 = Clock::now();
    for (int i = 0; i < TOTAL; ++i) {
        dns::Logger::info("flood message %d", i);
    }
    auto t1 = Clock::now();
    printf("  洪泛 %d 条消息耗时=%lld ms (入队应远快于消费)\n",
           TOTAL, elapsedMs(t0, t1));

    dns::Logger::shutdown();
    pool.shutdown();
    msleep(100);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("====== 组件级压力测试 ======\n\n");

    stressVector();
    stressString();
    stressLinkedHashMap();
    stressThreadPool();
    stressLogger();

    printf("\n====== 压测完成 ======\n");
    return 0;
}
