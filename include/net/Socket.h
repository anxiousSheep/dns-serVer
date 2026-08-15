// Socket.h — RAII UDP Socket + 地址包装
// 跨平台 socket 操作（Windows/Linux），析构时自动关闭
#pragma once

#include "../base/DnsString.h"
#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET 0x9800000C
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace dns {

// ==================== InetAddress ====================
struct InetAddress {
    uint32_t ip;     // 网络字节序 (sin_addr.s_addr)
    uint16_t port;   // 网络字节序 (sin_port)

    InetAddress() : ip(INADDR_ANY), port(0) {}

    InetAddress(const char* ipStr, uint16_t p) : port(htons(p)) {
        ip = inet_addr(ipStr);
    }

    // 从 sockaddr_in 构造
    static InetAddress fromSockaddr(const struct sockaddr_in& addr) {
        InetAddress a;
        a.ip   = addr.sin_addr.s_addr;
        a.port = addr.sin_port;
        return a;
    }

    // 填充 sockaddr_in
    void fillSockaddr(struct sockaddr_in& addr) const {
        addr.sin_family = AF_INET;
        addr.sin_port   = port;
        addr.sin_addr.s_addr = ip;
    }

    // 转为可读字符串
    String toString() const {
        char buf[32];
        unsigned char* b = (unsigned char*)&ip;
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d:%d", b[0], b[1], b[2], b[3], ntohs(port));
        return String(buf);
    }
};

// ==================== 跨平台 close socket ====================
inline void closeSocket(int fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

// ==================== UdpSocket ====================
class UdpSocket {
public:
    UdpSocket() : fd_(-1) {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    }

    ~UdpSocket() {
        if (fd_ >= 0) closeSocket(fd_);
    }

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    UdpSocket& operator=(UdpSocket&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) closeSocket(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    // 绑定端口
    bool bind(uint16_t port) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        int ret = ::bind(fd_, (struct sockaddr*)&addr, sizeof(addr));
        if (ret < 0) return false;

#ifdef _WIN32
        // 禁用 WSAECONNRESET
        DWORD bytesReturned = 0;
        BOOL  newBehavior = FALSE;
        WSAIoctl(fd_, SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior),
                 nullptr, 0, &bytesReturned, nullptr, nullptr);
#endif
        return true;
    }

    // 发送
    int sendto(const void* buf, int len, const InetAddress& to) {
        struct sockaddr_in addr;
        to.fillSockaddr(addr);
        return ::sendto(fd_,
#ifdef _WIN32
                        (const char*)buf,
#else
                        buf,
#endif
                        len, 0, (struct sockaddr*)&addr, sizeof(addr));
    }

    // 接收
    int recvfrom(void* buf, int maxLen, InetAddress& from) {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);
        int ret = ::recvfrom(fd_,
#ifdef _WIN32
                             (char*)buf,
#else
                             buf,
#endif
                             maxLen, 0, (struct sockaddr*)&addr, &addrLen);
        if (ret >= 0) {
            from = InetAddress::fromSockaddr(addr);
        }
        return ret;
    }

    // 设置接收超时（毫秒）
    void setRecvTimeout(int ms) {
#ifdef _WIN32
        DWORD timeout = ms;
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
        struct timeval tv;
        tv.tv_sec  = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    }

    int  fd()     const { return fd_; }
    bool isValid() const { return fd_ >= 0; }

private:
    int fd_;
};

} // namespace dns
