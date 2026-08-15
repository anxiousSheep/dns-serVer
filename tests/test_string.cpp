// test_string.cpp — String (SSO) 功能测试
#include "DnsString.h"
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
    printf("=== String Tests ===\n\n");

    // Test 1: SSO, 14 bytes (within SSO_CAP=15)
    {
        printf("1. SSO 14 bytes\n");
        dns::String s("www.baidu.com");  // 13 bytes
        TEST("length==13", s.length() == 13);
        TEST("c_str correct", strcmp(s.c_str(), "www.baidu.com") == 0);
        TEST("not empty", !s.empty());
    }

    // Test 2: SSO, 14 bytes (SSO boundary)
    {
        printf("2. SSO 14 bytes (boundary)\n");
        dns::String s("0123456789abcd");  // 14 bytes, should stay in SSO
        TEST("length==14", s.length() == 14);
        TEST("correct content", strcmp(s.c_str(), "0123456789abcd") == 0);
    }

    // Test 3: 15 bytes (exceeds SSO, triggers heap alloc)
    {
        printf("3. 15 bytes (heap)\n");
        dns::String s("0123456789abcde");  // 15 bytes, should go to heap
        TEST("length==15", s.length() == 15);
        TEST("correct content", strcmp(s.c_str(), "0123456789abcde") == 0);
    }

    // Test 4: 100 bytes (heap)
    {
        printf("4. 100 bytes (heap)\n");
        const char* longStr = "01234567890123456789012345678901234567890123456789"
                              "01234567890123456789012345678901234567890123456789";
        dns::String s(longStr);
        TEST("length==100", s.length() == 100);
        TEST("correct content", strcmp(s.c_str(), longStr) == 0);
    }

    // Test 5: 拷贝构造
    {
        printf("5. 拷贝构造\n");
        dns::String s1("hello world");
        dns::String s2(s1);

        TEST("s2 length correct", s2.length() == s1.length());
        TEST("s2 content correct", strcmp(s2.c_str(), "hello world") == 0);
        TEST("s2 == s1", s1 == s2);

        // In-place modification
        dns::String s3("different");
        s2 = s3;
        TEST("s2 modified independent of s1", strcmp(s2.c_str(), "different") == 0);
        TEST("s1 unchanged", strcmp(s1.c_str(), "hello world") == 0);
    }

    // Test 6: 移动构造
    {
        printf("6. 移动构造\n");
        dns::String s1("move me");
        dns::String s2(dns::move(s1));

        TEST("s2 has content", strcmp(s2.c_str(), "move me") == 0);
        TEST("s1 empty after move", s1.empty());
    }

    // Test 7: append
    {
        printf("7. append\n");
        dns::String s("hello");
        s.append(" world");

        TEST("length==11", s.length() == 11);
        TEST("content==hello world", strcmp(s.c_str(), "hello world") == 0);

        // Append across SSO→heap boundary
        dns::String s2("0123456789ab");  // 12 bytes, within SSO
        s2.append("cdefghij");            // +8 = 20 bytes, exceeds SSO(14)
        TEST("SSO→heap append length==20", s2.length() == 20);
        TEST("SSO→heap content correct", strcmp(s2.c_str(), "0123456789abcdefghij") == 0);
    }

    // Test 8: c_str() null-terminated
    {
        printf("8. c_str() returns null-terminated\n");
        dns::String s("test");
        const char* p = s.c_str();
        TEST("c_str not null", p != nullptr);
        TEST("null-terminated", p[4] == '\0');

        // Empty string
        dns::String e;
        TEST("empty c_str not null", e.c_str() != nullptr);
        TEST("empty null-terminated", e.c_str()[0] == '\0');
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
