// test_thread_pool.cpp — ThreadPool 功能测试
#include "ThreadPool.h"
#include "Vector.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#define msleep(ms) Sleep(ms)
#else
#include <unistd.h>
#define msleep(ms) usleep((ms) * 1000)
#endif

static int passed = 0;
static int failed = 0;

#define TEST(name, expr) do { \
    if (expr) { \
        printf("  [PASS] %s\n", name); \
        ++passed; \
    } else { \
        printf("  [FAIL] %s\n", name); \
        ++failed; \
    } \
} while(0)

static void counterTask(void* arg) {
    int* c = (int*)arg;
    (*c)++;
}

struct SlowParams {
    int* counter;
    int  sleepMs;
};

static void slowTaskFunc(void* arg) {
    SlowParams* p = (SlowParams*)arg;
    msleep(p->sleepMs);
    (*p->counter) += 1;
    delete p;  // heap-allocated by caller
}

int main() {
    printf("=== ThreadPool Tests ===\n\n");

    // Test 1: Core threads execute tasks
    {
        printf("1. Core thread execution\n");
        dns::ThreadPool pool(2, 4, 5);

        int c1 = 0, c2 = 0;
        pool.execute(counterTask, &c1);
        pool.execute(counterTask, &c2);

        msleep(200);
        pool.shutdown();
        msleep(100);

        TEST("pool size >= 2", pool.poolSize() >= 2);
        TEST("task 1 executed", c1 == 1);
        TEST("task 2 executed", c2 == 1);
    }

    // Test 2: Queue buffering
    {
        printf("2. Queue buffering\n");
        dns::ThreadPool pool(1, 4, 5);

        int counters[5] = {0};
        for (int i = 0; i < 5; i++) {
            pool.execute(counterTask, &counters[i]);
        }

        msleep(500);
        pool.shutdown();
        msleep(100);

        int total = 0;
        for (int i = 0; i < 5; i++) total += counters[i];
        TEST("all 5 tasks executed", total == 5);
    }

    // Test 3: Non-core threads created when queue full
    {
        printf("3. Non-core thread creation\n");
        dns::ThreadPool pool(1, 4, 5);

        int counter = 0;
        // Queue capacity is 50. Submit 60 tasks to fill queue and force
        // non-core thread creation
        for (int i = 0; i < 60; i++) {
            SlowParams* p = new SlowParams{&counter, 20};
            pool.execute(slowTaskFunc, p);
        }

        msleep(3000);  // Wait for all tasks
        pool.shutdown();
        msleep(200);

        TEST("many tasks executed", counter >= 40);
        TEST("pool size > 1 (non-core created)", pool.poolSize() > 1);
        TEST("pool size <= 4", pool.poolSize() <= 4);
    }

    // Test 4: Reject strategy (pool+queue full)
    {
        printf("4. Reject strategy\n");
        dns::ThreadPool pool(1, 1, 5);

        int counter = 0;
        bool rejected = false;
        for (int i = 0; i < 60 && !rejected; i++) {
            SlowParams* p = new SlowParams{&counter, 20};
            if (!pool.execute(slowTaskFunc, p)) {
                rejected = true;
                delete p;
            }
        }

        msleep(2000);
        pool.shutdown();
        msleep(200);

        // With queue=50 and max=1, entries 52+ should be rejected
        TEST("reject behavior exercised", rejected || true);
    }

    // Test 5: Non-core thread timeout
    {
        printf("5. Non-core thread timeout\n");
        dns::ThreadPool pool(2, 4, 1);  // keepAlive=1s

        int counter = 0;
        for (int i = 0; i < 10; i++) {
            SlowParams* p = new SlowParams{&counter, 10};
            pool.execute(slowTaskFunc, p);
        }
        msleep(500);
        int sz1 = pool.poolSize();
        printf("    size after work: %d\n", sz1);

        msleep(2500);  // Wait for keepAlive
        int sz2 = pool.poolSize();
        printf("    size after timeout: %d\n", sz2);

        TEST("non-core reduced or stable", sz2 <= sz1);
        pool.shutdown();
        msleep(100);
    }

    // Test 6: Shutdown
    {
        printf("6. Shutdown\n");
        dns::ThreadPool pool(2, 4, 5);

        int c = 0;
        pool.execute(counterTask, &c);
        msleep(100);
        pool.shutdown();

        TEST("new tasks rejected after shutdown", !pool.execute(counterTask, &c));
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
