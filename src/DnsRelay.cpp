// DnsRelay.cpp — DNS 中继管理器实现
#include "../include/DnsRelay.h"
#include "../include/Logger.h"
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace dns {

DnsRelay::DnsRelay()
    : nextLocalId(1), upstreamIp("8.8.8.8")
{
    // Initialize pending query table — set all entries to unused
    for (int i = 0; i < PENDING_SIZE; ++i) {
        pending[i].used = false;
    }
    pthread_mutex_init(&lock, nullptr);
}

DnsRelay::~DnsRelay() {
    pthread_mutex_destroy(&lock);
}

bool DnsRelay::init(const char* upstreamDnsIp) {
    if (!relaySocket.isValid()) {
        Logger::error("DnsRelay::init: failed to create relay socket");
        return false;
    }
    relaySocket.setRecvTimeout(1000);  // 1s 超时（epoll 接管后实际不用）
    upstreamIp = String(upstreamDnsIp);
    return true;
}

bool DnsRelay::submit(const uint8_t* buffer, int len,
                       const InetAddress& clientAddr,
                       const String& domainName, uint16_t qtype) {
    pthread_mutex_lock(&lock);

    cleanupStale();

    int slot = _M_findFreeSlot();
    if (slot < 0) {
        pthread_mutex_unlock(&lock);
        Logger::error("DnsRelay::submit: pending table full (%d entries)", PENDING_SIZE);
        return false;
    }

    // 分配本地 ID
    uint16_t localId = nextLocalId++;
    if (nextLocalId == 0) nextLocalId = 1;

    // 填充条目
    PendingQuery& pq = pending[slot];
    pq.localId    = localId;
    pq.originalId = ntohs(*(uint16_t*)buffer);
    pq.clientAddr = clientAddr;
    memcpy(pq.originalBuffer, buffer, len);
    pq.originalLen  = len;
    pq.domainName   = domainName;
    pq.qtype        = qtype;
    pq.timestamp    = time(nullptr);
    pq.used         = true;

    // 替换 Transaction ID 后发送
    uint8_t modifiedBuffer[512];
    memcpy(modifiedBuffer, buffer, len);
    *(uint16_t*)modifiedBuffer = htons(localId);

    InetAddress upstream(upstreamIp.c_str(), 53);
    int sent = relaySocket.sendto(modifiedBuffer, len, upstream);

    if (sent < 0) {
        pq.used = false;
        pthread_mutex_unlock(&lock);
        Logger::error("DnsRelay::submit: sendto failed for '%s'", domainName.c_str());
        return false;
    }

    Logger::debug("DnsRelay::submit: localId=%u origId=%u domain='%s' qtype=%d",
                  localId, pq.originalId, domainName.c_str(), qtype);
    pthread_mutex_unlock(&lock);
    return true;
}

PendingQuery DnsRelay::processResponse(uint8_t* response, int* respLen) {
    PendingQuery result;
    result.used = false;

    if (*respLen < 2) return result;

    uint16_t localId = ntohs(*(uint16_t*)response);
    Logger::debug("DnsRelay::processResponse: received %d bytes, localId=%u", *respLen, localId);

    pthread_mutex_lock(&lock);

    int idx = _M_findByLocalId(localId);
    if (idx < 0) {
        pthread_mutex_unlock(&lock);
        Logger::debug("DnsRelay::processResponse: no pending query for localId=%u (stale?)", localId);
        return result;
    }

    // 拷贝所有必要信息后立即释放槽位
    PendingQuery& pq = pending[idx];
    result.localId    = pq.localId;
    result.originalId = pq.originalId;
    result.clientAddr = pq.clientAddr;
    memcpy(result.originalBuffer, pq.originalBuffer, pq.originalLen);
    result.originalLen = pq.originalLen;
    result.domainName  = pq.domainName;
    result.qtype       = pq.qtype;
    result.used        = true;

    pq.used = false;  // 释放槽位
    pthread_mutex_unlock(&lock);

    // 还原客户端原始 Transaction ID
    *(uint16_t*)response = htons(result.originalId);

    return result;
}

void DnsRelay::cleanupStale() {
    time_t now = time(nullptr);
    for (int i = 0; i < PENDING_SIZE; i++) {
        if (pending[i].used && (now - pending[i].timestamp) > PENDING_TIMEOUT) {
            Logger::debug("DnsRelay::cleanupStale: evicting localId=%u domain='%s'",
                          pending[i].localId, pending[i].domainName.c_str());
            pending[i].used = false;
        }
    }
}

int DnsRelay::_M_findFreeSlot() {
    for (int i = 0; i < PENDING_SIZE; i++) {
        if (!pending[i].used) return i;
    }
    return -1;
}

int DnsRelay::_M_findByLocalId(uint16_t localId) {
    for (int i = 0; i < PENDING_SIZE; i++) {
        if (pending[i].used && pending[i].localId == localId) return i;
    }
    return -1;
}

} // namespace dns
