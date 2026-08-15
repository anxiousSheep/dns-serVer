// test_logger.cpp — Logger 功能测试
#include "ThreadPool.h"
#include "Logger.h"
#include <cstdio>
#include <cstring>

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

int main() {
    printf("=== Logger Tests ===\n\n");

    // Test 1: Async messages
    {
        printf("1. Async messages\n");
        dns::ThreadPool pool(2, 4, 5);
        dns::Logger::init(&pool, 2048);

        // These should not block or crash
        dns::Logger::error("test error message %d", 1);
        dns::Logger::info("test info message %d", 2);
        dns::Logger::debug("test debug message %d", 3);

        msleep(100);  // Give consumer time to process

        dns::Logger::shutdown();
        pool.shutdown();
        msleep(100);

        TEST("async messages sent without crash", true);
    }

    // Test 2: Queue full → discard (no blocking)
    {
        printf("2. Queue full → discard\n");
        dns::ThreadPool pool(1, 1, 5);
        dns::Logger::init(&pool, 4);  // tiny queue, fills fast

        // Send many messages; consumer won't keep up with tiny pool
        for (int i = 0; i < 100; i++) {
            dns::Logger::info("rapid message %d", i);
        }

        msleep(2000);  // Wait for consumer to drain

        dns::Logger::shutdown();
        pool.shutdown();
        msleep(100);

        TEST("queue overflow handled without crash", true);
    }

    // Test 3: Shutdown drains remaining
    {
        printf("3. Shutdown drains queue\n");
        dns::ThreadPool pool(2, 4, 5);
        dns::Logger::init(&pool, 256);

        dns::Logger::info("message before shutdown 1");
        dns::Logger::info("message before shutdown 2");

        msleep(200);

        dns::Logger::shutdown();  // Should drain before returning
        pool.shutdown();
        msleep(100);

        TEST("shutdown completed without crash", true);
    }

    // Test 4: Fallback mode (no init)
    {
        printf("4. Fallback sync mode\n");
        // Logger was already shutdown; calling error/info/debug should fall back
        // to synchronous fprintf (should not crash)
        dns::Logger::error("fallback error message");
        dns::Logger::info("fallback info message");
        dns::Logger::debug("fallback debug message");

        TEST("fallback mode doesn't crash", true);
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
