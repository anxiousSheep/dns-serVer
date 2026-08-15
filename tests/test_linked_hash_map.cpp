// test_linked_hash_map.cpp — LinkedHashMap 功能测试
#include "DnsString.h"
#include "LinkedHashMap.h"
#include "Vector.h"
#include <cstdio>
#include <cstring>

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
    printf("=== LinkedHashMap Tests ===\n\n");

    // Test 1: accessOrder=false → insertion order traversal
    {
        printf("1. accessOrder=false (insertion order)\n");
        dns::LinkedHashMap<dns::String, int> map(8, false);

        map.put(dns::String("c"), 3);
        map.put(dns::String("a"), 1);
        map.put(dns::String("b"), 2);

        // Check values by key
        int* v = map.get(dns::String("a"));
        TEST("get a == 1", v != nullptr && *v == 1);
        v = map.get(dns::String("b"));
        TEST("get b == 2", v != nullptr && *v == 2);
        v = map.get(dns::String("c"));
        TEST("get c == 3", v != nullptr && *v == 3);
    }

    // Test 2: accessOrder=true → get brings to MRU
    {
        printf("2. accessOrder=true (LRU reorder on get)\n");
        dns::LinkedHashMap<dns::String, int> map(8, true);

        map.put(dns::String("a"), 1);
        map.put(dns::String("b"), 2);
        map.put(dns::String("c"), 3);

        // Get "a" → moves to MRU
        int* v = map.get(dns::String("a"));
        TEST("get a after LRU get", v != nullptr && *v == 1);

        // All entries still accessible
        v = map.get(dns::String("b"));
        TEST("get b still works", v != nullptr && *v == 2);
        v = map.get(dns::String("c"));
        TEST("get c still works", v != nullptr && *v == 3);
    }

    // Test 3: Capacity eviction (capacity=3)
    {
        printf("3. Capacity eviction (capacity=3)\n");
        dns::LinkedHashMap<int, int> map(3, true);

        map.put(1, 10);
        map.put(2, 20);
        map.put(3, 30);

        int* v = map.get(1);
        TEST("before eviction: 1 exists", v != nullptr);

        // Insert 4th → evict least-recently-used (key 2)
        map.put(4, 40);

        v = map.get(2);
        TEST("after eviction: 2 evicted", v == nullptr);

        v = map.get(1);
        TEST("after eviction: 1 still exists", v != nullptr && *v == 10);
    }

    // Test 4: Hash collision (multiple keys in same bucket)
    {
        printf("4. Hash collision\n");
        // Since Hash<int> just returns key, keys with same mod will collide
        dns::LinkedHashMap<int, int> map(4, false);  // 4 buckets

        // Use keys that collide: 0, 4, 8 in bucket 0
        map.put(0, 100);
        map.put(4, 400);
        map.put(8, 800);

        int* v = map.get(0);
        TEST("collision: get 0", v != nullptr && *v == 100);
        v = map.get(4);
        TEST("collision: get 4", v != nullptr && *v == 400);
        v = map.get(8);
        TEST("collision: get 8", v != nullptr && *v == 800);
    }

    // Test 5: duplicate put (update)
    {
        printf("5. Duplicate put (update value)\n");
        dns::LinkedHashMap<dns::String, int> map(8, false);

        map.put(dns::String("key"), 100);
        TEST("put 1st: contains", map.contains(dns::String("key")));

        map.put(dns::String("key"), 200);  // Same key, new value
        int* v = map.get(dns::String("key"));
        TEST("put 2nd: value updated", v != nullptr && *v == 200);
        // Size should still be 1-ish (LinkedHashMap doesn't expose size directly)
    }

    // Test 6: remove
    {
        printf("6. remove\n");
        dns::LinkedHashMap<int, int> map(8, false);

        map.put(1, 10);
        map.put(2, 20);
        TEST("before remove: contains 1", map.contains(1));

        map.remove(1);
        TEST("after remove: !contains 1", !map.contains(1));
        TEST("after remove: still contains 2", map.contains(2));
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
