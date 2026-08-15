// test_vector.cpp — Vector 功能测试
#include "Vector.h"
#include <cstdio>

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
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Vector Tests ===\n\n");

    // Test 1: push_back 扩容
    {
        printf("1. push_back 扩容\n");
        dns::Vector<int> v;
        TEST("empty: size==0", v.size() == 0);
        TEST("empty: capacity==0", v.capacity() == 0);

        v.push_back(10);
        TEST("after 1 push: size==1", v.size() == 1);
        TEST("after 1 push: capacity==1", v.capacity() == 1);
        TEST("after 1 push: v[0]==10", v[0] == 10);

        v.push_back(20);
        TEST("after 2 push: size==2", v.size() == 2);
        TEST("after 2 push: capacity==2", v.capacity() == 2);

        v.push_back(30);
        TEST("after 3 push: size==3", v.size() == 3);
        TEST("after 3 push: capacity==4", v.capacity() == 4);

        v.push_back(40);
        v.push_back(50);
        TEST("after 5 push: size==5", v.size() == 5);
        TEST("after 5 push: capacity==8", v.capacity() == 8);
    }

    // Test 2: emplace_back
    {
        printf("2. emplace_back 原地构造\n");
        dns::Vector<int> v;
        v.emplace_back(42);
        TEST("emplace_back: size==1", v.size() == 1);
        TEST("emplace_back: v[0]==42", v[0] == 42);
    }

    // Test 3: 拷贝构造
    {
        printf("3. 拷贝构造\n");
        dns::Vector<int> v1;
        v1.push_back(1);
        v1.push_back(2);
        v1.push_back(3);

        dns::Vector<int> v2(v1);
        TEST("copy: v2.size==3", v2.size() == 3);
        TEST("copy: v2[0]==1", v2[0] == 1);
        TEST("copy: v2[1]==2", v2[1] == 2);
        TEST("copy: v2[2]==3", v2[2] == 3);

        v1[0] = 99;
        TEST("independent: v2[0] still 1", v2[0] == 1);
    }

    // Test 4: 移动构造
    {
        printf("4. 移动构造\n");
        dns::Vector<int> v1;
        v1.push_back(1);
        v1.push_back(2);

        dns::Vector<int> v2(dns::move(v1));
        TEST("move: v2.size==2", v2.size() == 2);
        TEST("move: v2[0]==1", v2[0] == 1);
    }

    // Test 5: pop_back
    {
        printf("5. pop_back\n");
        dns::Vector<int> v;
        v.push_back(10);
        v.push_back(20);
        v.push_back(30);
        TEST("before pop: size==3", v.size() == 3);

        v.pop_back();
        TEST("after pop: size==2", v.size() == 2);
        TEST("after pop: v[1]==20", v[1] == 20);

        v.pop_back();
        TEST("after 2nd pop: size==1", v.size() == 1);

        v.pop_back();
        TEST("after 3rd pop: size==0", v.size() == 0);
    }

    // Test 6: clear
    {
        printf("6. clear\n");
        dns::Vector<int> v;
        v.push_back(1);
        v.push_back(2);
        v.push_back(3);
        size_t capBefore = v.capacity();

        v.clear();
        TEST("clear: size==0", v.size() == 0);
        TEST("clear: capacity unchanged", v.capacity() == capBefore);
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
