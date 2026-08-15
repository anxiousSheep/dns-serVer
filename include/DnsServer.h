// DnsServer.h — DNS 服务器主控
// 组装所有模块：epoll 事件循环、线程池、LRU 缓存、DNS 中继
#pragma once

#include "../base/LinkedHashMap.h"
#include "../base/SmartPtr.h"
#include "../base/DnsString.h"
#include "../base/Vector.h"
#include "../net/Epoll.h"
#include "../net/Socket.h"
#include "ThreadPool.h"
#include "Logger.h"
#include "DnsProtocol.h"
#include "DnsRelay.h"
#include "Config.h"
#include <pthread.h>
#include <cstdint>
#include <cstdio>

namespace dns {

class DnsServer {
public:
    explicit DnsServer(const Config& config);
    ~DnsServer();

    DnsServer(const DnsServer&) = delete;
    DnsServer& operator=(const DnsServer&) = delete;

    // 启动主循环（阻塞）
    void run();

    // 发送停止信号
    void stop();

private:
    // ===== 配置 =====
    Config config;

    // ===== 核心模块 =====
    ThreadPool    threadPool;
    LinkedHashMap<String, Vector<String>> localCache;  // 本地中继表
    LinkedHashMap<String, Vector<String>> dnsCache;    // 上游响应缓存

    // ===== 网络 =====
    UdpSocket serverSocket;   // 监听 53 端口
    DnsRelay  relay;         // 中继管理器（包含 relay socket）
    Epoller   epoller;

    // ===== 并发控制 =====
    pthread_mutex_t sendLock;       // 保护多线程 sendto
    pthread_mutex_t queryIdLock;    // 保护 queryId 映射表
    bool            stop_;

    // ===== 查询 ID 映射 =====
    // 键：(clientIp, clientPort, clientDnsId) → 值：自增 serverId
    static const int QUERY_ID_MAP_SIZE = 128;
    static const int QUERY_ID_TIMEOUT  = 60;

    struct QueryIdEntry {
        uint32_t clientIp;
        uint16_t clientPort;
        uint16_t clientDnsId;
        uint16_t serverId;
        time_t   timestamp;
        bool     used;
    };
    QueryIdEntry queryIdMap[QUERY_ID_MAP_SIZE];
    uint16_t     nextQueryId;

    // ===== 查询记录 =====
    FILE*          queryLogFile;
    pthread_mutex_t queryLogLock;

    // ===== 事件处理（主线程调用） =====
    void _M_onClientQuery();
    void _M_onRelayResponse();
    void _M_onTimeout();

    // ===== 业务逻辑（线程池调用） =====
    void _M_handleDnsQuery(const uint8_t* buffer, int len, const InetAddress& client);
    static void _M_handleDnsQueryEntry(void* arg);

    // ===== 辅助函数 =====
    uint16_t _M_assignQueryId(uint32_t clientIp, uint16_t clientPort, uint16_t clientDnsId);
    void _M_printQueryRecord(uint16_t serverId, const char* clientIp,
                             const char* domain, const char* qtypeStr,
                             const char* action, const char* result);
    void _M_sendResponse(const uint8_t* data, int len, const InetAddress& to);
    void _M_cleanupQueryIdMap();

    // Query ID 映射表操作
    int _M_findQueryIdEntry(uint32_t ip, uint16_t port, uint16_t dnsId);
};

} // namespace dns
