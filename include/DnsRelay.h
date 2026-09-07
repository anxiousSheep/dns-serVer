// DnsRelay.h — DNS 中继管理器
// 持久 socket + 消息 ID 映射 + 独立接收线程 → epoll 事件驱动
// 将客户端原始 Transaction ID 替换为本地自增 ID 后发往上游 DNS
#pragma once

#include "../base/Vector.h"
#include "../base/DnsString.h"
#include "../net/Socket.h"
#include <pthread.h>
#include <ctime>
#include <cstdint>

namespace dns {

// 待处理查询的完整状态
struct PendingQuery {
    uint16_t   localId;       // 本地分配的 Transaction ID
    uint16_t   originalId;    // 客户端原始 Transaction ID
    InetAddress clientAddr;   // 客户端地址
    uint8_t    originalBuffer[512]; // 客户端原始查询报文
    int        originalLen;
    String     domainName;    // 解码后的域名
    uint16_t   qtype;         // 查询类型（1=A 记录）
    time_t     timestamp;     // 创建时间
    bool       used;
};

class DnsRelay {
public:
    static const int PENDING_SIZE   = 256;
    static const int PENDING_TIMEOUT = 10;  // 秒

    DnsRelay();
    ~DnsRelay();

    DnsRelay(const DnsRelay&) = delete;
    DnsRelay& operator=(const DnsRelay&) = delete;

    // 初始化中继 socket（不可移动的设计：socket 绑定后地址固定）
    bool init(const char* upstreamDnsIp);

    // 提交一次中继查询（业务线程调用）
    // 返回 true=成功发送, false=失败（映射表满或网络错误）
    bool submit(const uint8_t* buffer, int len,
                const InetAddress& clientAddr,
                const String& domainName, uint16_t qtype);

    // 处理上游响应（epoll 回调，主线程调用）
    // 返回的 PendingQuery 含还原后的响应（originalId 已写回），
    // 调用者负责 sendto 回传客户端 + 输出查询记录
    // 未找到对应 pending 条目时返回 used=false
    PendingQuery processResponse(uint8_t* response, int* respLen);

    // 超时清理（epoll 超时回调，主线程调用）
    void cleanupStale();

    // 访问 relay socket fd（用于 epoll 注册）
    int relayFd() const { return relaySocket.fd(); }

    // 从上游 socket 接收数据（epoll 触发后调用）
    int recvFromUpstream(void* buf, int maxLen, InetAddress& from) {
        return relaySocket.recvfrom(buf, maxLen, from);
    }

private:
    UdpSocket     relaySocket;
    PendingQuery  pending[PENDING_SIZE];
    uint16_t      nextLocalId;
    String        upstreamIp;

    pthread_mutex_t lock;

    // 内部查找
    int  findFreeSlot();
    int  findByLocalId(uint16_t localId);
};

} // namespace dns
