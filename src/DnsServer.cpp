// DnsServer.cpp — DNS 服务器主控实现
#include "../include/DnsServer.h"
#include "../include/DnsProtocol.h"
#include "../include/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace dns {

// 线程池任务封装结构
struct DnsQueryTask {
    uint8_t     buffer[512];
    int         len;
    InetAddress clientAddr;
    DnsServer*  server;    // 回指针，用于回调
};

DnsServer::DnsServer(const Config& config)
    : config(config),
      threadPool(4, 8, 60), // core=4, max=8, keepAlive=60s
      localCache(1024, false), // 不开启LRU
      dnsCache(256, true), // LRU 序
      stop_(false),
      nextQueryId(1)
{
    // 初始化日志
    Logger::init(&threadPool, 2048);
    Logger::setDebugLevel(config.debugLevel);
    Logger::info("Logger initialized, queue capacity=2048");

    // 初始化锁
    pthread_mutex_init(&sendLock, nullptr);
    pthread_mutex_init(&queryIdLock, nullptr);
    pthread_mutex_init(&queryLogLock, nullptr);

    // 初始化 Query ID 映射表
    memset(queryIdMap, 0, sizeof(queryIdMap));

    // 打开查询记录文件
    queryLogFile = fopen("dns_query.log", "a");

    // 绑定 53 端口
    if (!serverSocket.bind(53)) {
        Logger::error("Failed to bind UDP socket to port 53");
    }
    Logger::info("UDP socket bound to 0.0.0.0:53");

    // 初始化 DNS 中继
    if (!relay.init(config.dnsServer.c_str())) {
        Logger::error("Failed to initialize DNS relay");
    }
    Logger::info("DNS relay initialized, upstream=%s", config.dnsServer.c_str());

    // 加载本地中继表
    // 注意：loadDnsRelayTable 使用模板，需遍历加载
    FILE* fp = fopen(config.filename.c_str(), "r");
    if (fp) {
        char line[256];
        int count = 0;
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '\0' || line[0] == '#') continue;

            char ipStr[32], domain[128];
            if (sscanf(line, "%31s %127s", ipStr, domain) == 2) {
                String domainKey(domain);
                Vector<String>* ips = localCache.get(domainKey);
                if (ips) {
                    ips->push_back(String(ipStr));
                } else {
                    Vector<String> newList;
                    newList.push_back(String(ipStr));
                    localCache.put(domainKey, newList);
                }
                count++;
            }
        }
        fclose(fp);
        Logger::info("Loaded %d entries from '%s' into local cache", count, config.filename.c_str());
    } else {
        Logger::error("Failed to open config file '%s', local cache empty", config.filename.c_str());
    }
}

DnsServer::~DnsServer() {
    stop();

    if (queryLogFile) fclose(queryLogFile);

    pthread_mutex_destroy(&sendLock);
    pthread_mutex_destroy(&queryIdLock);
    pthread_mutex_destroy(&queryLogLock);

    Logger::shutdown();
}

void DnsServer::run() {
    // 注册两个 socket 到 epoll
    epoller.add(serverSocket.fd(), EPOLLIN);
    epoller.add(relay.relayFd(), EPOLLIN);

    Logger::info("DNS Server running, epoll event loop started");

    while (!stop_) {
        auto events = epoller.wait(1000);  // 1s 超时

        for (auto& ev : events) {
            if (ev.fd == serverSocket.fd()) {
                onClientQuery();
            } else if (ev.fd == relay.relayFd()) {
                onRelayResponse();
            }
            if (stop_) break;
        }

        // 超时 → 定时清理
        if (events.empty()) {
            onTimeout();
        }
    }
}

void DnsServer::stop() {
    stop_ = true;
    threadPool.shutdown();
}

// ==================== 事件处理 ====================

void DnsServer::onClientQuery() {
    DnsQueryTask* task = new DnsQueryTask();
    task->server = this;

    int len = serverSocket.recvfrom(task->buffer, sizeof(task->buffer), task->clientAddr);
    task->len = len;

    if (len < 0) {
        Logger::error("recvfrom failed on server socket");
        delete task;
        return;
    }

    Logger::debug("Received %d bytes from client", len);

    if (!threadPool.execute(handleDnsQueryEntry, task)) {
        Logger::error("ThreadPool::execute failed, dropping query");
        delete task;
    }
}

void DnsServer::onRelayResponse() {
    uint8_t response[512];
    InetAddress from;
    int respLen = relay.recvFromUpstream(response, sizeof(response), from);

    if (respLen < 0) return;
    if (respLen < 2) return;

    // 通过中继管理器处理
    // relay socket 接收
    PendingQuery pq = relay.processResponse(response, &respLen);

    if (!pq.used) return;

    char clientIpStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &pq.clientAddr.ip, clientIpStr, sizeof(clientIpStr));

    if (pq.qtype == 1) {
        // A 记录：解析 → 缓存 → 回传
        Vector<String> ips = DnsProtocol::parseARecords(response, respLen);

        if (!ips.empty()) {
            // 写入 DNS 缓存
            dnsCache.put(pq.domainName, ips);

            // 构造结果字符串
            char resultStr[256] = {0};
            for (size_t i = 0; i < ips.size(); i++) {
                if (i > 0) { strcat(resultStr, ", "); }
                strcat(resultStr, ips[i].c_str());
            }

            // 透传上游响应给客户端
            sendResponse(response, respLen, pq.clientAddr);

            uint16_t serverId = assignQueryId(pq.clientAddr.ip, pq.clientAddr.port, pq.originalId);
            printQueryRecord(serverId, clientIpStr, pq.domainName.c_str(),
                               "A", "Relay", resultStr);
        } else {
            uint16_t serverId = assignQueryId(pq.clientAddr.ip, pq.clientAddr.port, pq.originalId);
            printQueryRecord(serverId, clientIpStr, pq.domainName.c_str(),
                               "A", "Relay", "ParseFail");
        }
    } else {
        // 非 A 记录：原样透传
        sendResponse(response, respLen, pq.clientAddr);

        uint16_t serverId = assignQueryId(pq.clientAddr.ip, pq.clientAddr.port, pq.originalId);
        printQueryRecord(serverId, clientIpStr, pq.domainName.c_str(),
                           pq.qtype == 28 ? "AAAA" : "OTHER", "Relay", "FwdOK");
    }
}

void DnsServer::onTimeout() {
    cleanupQueryIdMap();
    relay.cleanupStale();
}

// ==================== 业务逻辑 ====================

void DnsServer::handleDnsQueryEntry(void* arg) {
    DnsQueryTask* task = static_cast<DnsQueryTask*>(arg);
    task->server->handleDnsQuery(task->buffer, task->len, task->clientAddr);
    delete task;
}

void DnsServer::handleDnsQuery(const uint8_t* buffer, int len, const InetAddress& client) {
    // 分配查询 ID
    uint16_t clientDnsId = (len >= 2) ? ntohs(*(uint16_t*)buffer) : 0;
    uint16_t serverId = assignQueryId(client.ip, client.port, clientDnsId);

    String clientIpStr = client.toString();
    // 截取端口部分（简化处理）
    char ipOnly[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client.ip, ipOnly, sizeof(ipOnly));

    // 提取 QTYPE
    uint16_t qtype = DnsProtocol::extractQtype(buffer, len);

    // 解码域名
    const uint8_t* qname = buffer + 12;
    int qnameLen = 0;
    if (len > 12) {
        while (qname + qnameLen < buffer + len) {
            uint8_t c = qname[qnameLen];
            if (c == 0) { qnameLen++; break; }
            if ((c & 0xC0) == 0xC0) { qnameLen += 2; break; }
            qnameLen += c + 1;
        }
    }
    String domain = DnsProtocol::decodeDomainName(qname, qnameLen);
    Logger::info("Decoded domain: %s (qtype=%d)", domain.c_str(), qtype);

    uint8_t response[512];
    int respLen = -1;

    // 非 A 记录 → 直接中继
    if (qtype != 1) {
        Logger::info("Non-A query '%s' type=%d, relaying", domain.c_str(), qtype);
        relay.submit(buffer, len, client, domain, qtype);
        return;
    }

    // A 记录查询
    // 1. 查本地缓存
    Vector<String>* localIps = localCache.get(domain);
    if (localIps && !localIps->empty()) {
        // 判断是否屏蔽
        bool blocked = (localIps->size() == 1 && (*localIps)[0] == String("0.0.0.0"));
        const char* action = blocked ? "Block" : "Local";

        // 构造结果字符串
        char resultStr[256] = {0};
        for (size_t i = 0; i < localIps->size(); i++) {
            if (i > 0) { strcat(resultStr, ", "); }
            strcat(resultStr, (*localIps)[i].c_str());
        }

        respLen = DnsProtocol::buildResponse(buffer, len, *localIps, response, sizeof(response));
        if (respLen > 0) {
            sendResponse(response, respLen, client);
            printQueryRecord(serverId, ipOnly, domain.c_str(), "A", action, resultStr);
        } else {
            printQueryRecord(serverId, ipOnly, domain.c_str(), "A", action, "BuildFail");
        }
        return;
    }

    // 2. 查 DNS 缓存
    Vector<String>* cachedIps = dnsCache.get(domain);
    if (cachedIps && !cachedIps->empty()) {
        char resultStr[256] = {0};
        for (size_t i = 0; i < cachedIps->size(); i++) {
            if (i > 0) { strcat(resultStr, ", "); }
            strcat(resultStr, (*cachedIps)[i].c_str());
        }

        respLen = DnsProtocol::buildResponse(buffer, len, *cachedIps, response, sizeof(response));
        if (respLen > 0) {
            sendResponse(response, respLen, client);
            printQueryRecord(serverId, ipOnly, domain.c_str(), "A", "Local", resultStr);
        }
        return;
    }

    // 3. 缓存未命中 → 中继到上游
    Logger::info("Domain '%s' not in cache, submitting to relay", domain.c_str());
    if (!relay.submit(buffer, len, client, domain, 1)) {
        // 中继失败 → NXDOMAIN
        Logger::error("Relay submit failed for '%s', returning NXDOMAIN", domain.c_str());
        respLen = DnsProtocol::buildNxdomain(buffer, len, response, sizeof(response));
        if (respLen > 0) {
            sendResponse(response, respLen, client);
        }
        printQueryRecord(serverId, ipOnly, domain.c_str(), "A", "Relay", "SubmitFail");
    }
}

// ==================== 辅助函数 ====================

uint16_t DnsServer::assignQueryId(uint32_t clientIp, uint16_t clientPort, uint16_t clientDnsId) {
    pthread_mutex_lock(&queryIdLock);
    time_t now = time(nullptr);

    // 查找已有映射
    for (int i = 0; i < QUERY_ID_MAP_SIZE; i++) {
        if (!queryIdMap[i].used) continue;
        if (now - queryIdMap[i].timestamp > QUERY_ID_TIMEOUT) {
            queryIdMap[i].used = false;
            continue;
        }
        if (queryIdMap[i].clientIp    == clientIp &&
            queryIdMap[i].clientPort  == clientPort &&
            queryIdMap[i].clientDnsId == clientDnsId) {
            // 重传，复用已有 ID
            queryIdMap[i].timestamp = now;
            uint16_t id = queryIdMap[i].serverId;
            pthread_mutex_unlock(&queryIdLock);
            return id;
        }
    }

    // 新请求
    uint16_t id = nextQueryId++;
    if (nextQueryId == 0) nextQueryId = 1;

    // 存入（优先空闲/超时槽位）
    int slot = -1;
    time_t oldest = now;
    for (int i = 0; i < QUERY_ID_MAP_SIZE; i++) {
        if (!queryIdMap[i].used ||
            now - queryIdMap[i].timestamp > QUERY_ID_TIMEOUT) {
            slot = i;
            break;
        }
        if (queryIdMap[i].timestamp < oldest) {
            oldest = queryIdMap[i].timestamp;
            slot = i;
        }
    }
    if (slot >= 0) {
        queryIdMap[slot].clientIp    = clientIp;
        queryIdMap[slot].clientPort  = clientPort;
        queryIdMap[slot].clientDnsId = clientDnsId;
        queryIdMap[slot].serverId    = id;
        queryIdMap[slot].timestamp   = now;
        queryIdMap[slot].used        = true;
    }

    pthread_mutex_unlock(&queryIdLock);
    return id;
}

void DnsServer::cleanupQueryIdMap() {
    pthread_mutex_lock(&queryIdLock);
    time_t now = time(nullptr);
    for (int i = 0; i < QUERY_ID_MAP_SIZE; i++) {
        if (queryIdMap[i].used && now - queryIdMap[i].timestamp > QUERY_ID_TIMEOUT) {
            queryIdMap[i].used = false;
        }
    }
    pthread_mutex_unlock(&queryIdLock);
}

void DnsServer::printQueryRecord(uint16_t serverId, const char* clientIp,
                                     const char* domain, const char* qtypeStr,
                                     const char* action, const char* result) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char timeBuf[16];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", t);

    pthread_mutex_lock(&queryLogLock);

    fprintf(stdout, "[%05u] %s %s %s %s %s %s\n",
            serverId, timeBuf, clientIp, qtypeStr, domain, action, result);
    fflush(stdout);

    if (queryLogFile) {
        fprintf(queryLogFile, "[%05u] %s %s %s %s %s %s\n",
                serverId, timeBuf, clientIp, qtypeStr, domain, action, result);
        fflush(queryLogFile);
    }

    pthread_mutex_unlock(&queryLogLock);
}

void DnsServer::sendResponse(const uint8_t* data, int len, const InetAddress& to) {
    pthread_mutex_lock(&sendLock);
    int sent = serverSocket.sendto(data, len, to);
    pthread_mutex_unlock(&sendLock);

    if (sent < 0) {
        Logger::error("sendto failed");
    }
}

} // namespace dns
