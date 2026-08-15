// stress_network.cpp — 网络级压力测试
// 多线程并发 UDP 查询打满运行中的 DNS 服务器，统计吞吐 / 成功 / 超时 / 延迟
// 用法: stress_network [线程数] [每线程查询数] [域名] [端口]
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <pthread.h>

using Clock = std::chrono::steady_clock;

static long long elapsedMs(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

// 构造 DNS A 查询报文（header + QNAME + QTYPE=A + QCLASS=IN），返回报文长度
static int buildAQuery(const char* domain, uint16_t id, uint8_t* buf) {
    uint16_t flags = htons(0x0100);  // RD=1
    uint16_t qd = htons(1), zero = 0;
    memcpy(buf + 0, &id, 2);
    memcpy(buf + 2, &flags, 2);
    memcpy(buf + 4, &qd, 2);
    memcpy(buf + 6, &zero, 2);
    memcpy(buf + 8, &zero, 2);
    memcpy(buf + 10, &zero, 2);
    int pos = 12;

    // QNAME：按 '.' 切分标签编码
    const char* p = domain;
    while (*p) {
        const char* dot = strchr(p, '.');
        int len = dot ? (int)(dot - p) : (int)strlen(p);
        buf[pos++] = (uint8_t)len;
        memcpy(buf + pos, p, len);
        pos += len;
        if (!dot) break;
        p = dot + 1;
    }
    buf[pos++] = 0;

    uint16_t qtype = htons(1), qclass = htons(1);
    memcpy(buf + pos, &qtype, 2); pos += 2;
    memcpy(buf + pos, &qclass, 2); pos += 2;
    return pos;
}

struct WorkerArgs {
    const char* domain;
    int         port;
    int         queries;
    uint16_t    id;
    std::atomic<int>*        success;
    std::atomic<int>*        timeout;
    std::atomic<long long>*  totalLatencyUs;
};

static void closeSock(int fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

static void setRecvTimeout(int fd, int ms) {
#ifdef _WIN32
    DWORD t = ms;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

static void* worker(void* arg) {
    WorkerArgs* a = static_cast<WorkerArgs*>(arg);
    int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return nullptr;

    setRecvTimeout(fd, 2000);  // 2 秒超时

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons((uint16_t)a->port);
    srv.sin_addr.s_addr = inet_addr("127.0.0.1");

    uint8_t query[512];
    int qlen = buildAQuery(a->domain, a->id, query);

    uint8_t resp[512];
    for (int i = 0; i < a->queries; ++i) {
        auto t0 = Clock::now();
        int n = sendto(fd, (const char*)query, qlen, 0,
                       (struct sockaddr*)&srv, sizeof(srv));
        if (n < 0) { ++(*a->timeout); continue; }

        struct sockaddr_in from;
        socklen_t fromLen = sizeof(from);
        n = recvfrom(fd, (char*)resp, sizeof(resp), 0,
                     (struct sockaddr*)&from, &fromLen);
        auto t1 = Clock::now();
        if (n < 0) { ++(*a->timeout); continue; }

        // 校验：ID 一致 + 是响应(QR=1) + RCODE=0
        bool ok = (n >= 12) && resp[0] == query[0] && resp[1] == query[1]
               && (resp[2] & 0x80) && ((resp[3] & 0x0F) == 0);
        if (ok) {
            ++(*a->success);
            a->totalLatencyUs->fetch_add(
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        } else {
            ++(*a->timeout);  // 收到但非法，计为失败
        }
    }

    closeSock(fd);
    return nullptr;
}

int main(int argc, char* argv[]) {
    int threads = argc > 1 ? atoi(argv[1]) : 100;
    int queries = argc > 2 ? atoi(argv[2]) : 100;
    const char* domain = argc > 3 ? argv[3] : "mytest.local";
    int port = argc > 4 ? atoi(argv[4]) : 53;
    if (threads <= 0) threads = 100;
    if (queries <= 0) queries = 100;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    printf("====== 网络级压力测试 ======\n");
    printf("  目标     = 127.0.0.1:%d\n", port);
    printf("  域名     = %s\n", domain);
    printf("  并发     = %d 线程 x %d 查询 = %d 总查询\n",
           threads, queries, threads * queries);
    printf("  超时     = 2000 ms\n\n");

    std::atomic<int> success{0};
    std::atomic<int> timeout{0};
    std::atomic<long long> totalLatencyUs{0};

    pthread_t* tids = new pthread_t[threads];
    WorkerArgs* args = new WorkerArgs[threads];
    auto t0 = Clock::now();
    for (int i = 0; i < threads; ++i) {
        args[i] = WorkerArgs{domain, port, queries, (uint16_t)(i + 1),
                             &success, &timeout, &totalLatencyUs};
        pthread_create(&tids[i], nullptr, worker, &args[i]);
    }
    for (int i = 0; i < threads; ++i) pthread_join(tids[i], nullptr);
    auto t1 = Clock::now();
    long long ms = elapsedMs(t0, t1);

    int total = threads * queries;
    int s = success.load();
    int f = timeout.load();
    double qps = ms > 0 ? (double)s * 1000.0 / ms : 0.0;
    double avgUs = s > 0 ? (double)totalLatencyUs.load() / s : 0.0;

    printf("====== 结果 ======\n");
    printf("  总查询     = %d\n", total);
    printf("  成功       = %d (%.1f%%)\n", s, total > 0 ? 100.0 * s / total : 0.0);
    printf("  失败/超时  = %d (%.1f%%)\n", f, total > 0 ? 100.0 * f / total : 0.0);
    printf("  总耗时     = %lld ms\n", ms);
    printf("  吞吐       = %.1f 查询/s\n", qps);
    printf("  平均延迟   = %.2f ms\n", avgUs / 1000.0);

    if (s == 0) {
        printf("\n  [提示] 成功数为 0：请确认服务器已启动并绑定 53 端口。\n");
    }

    delete[] tids;
    delete[] args;
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
