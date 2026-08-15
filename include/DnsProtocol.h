// DnsProtocol.h — DNS 协议编解码工具类
// 纯函数，无状态：域名解码、A记录解析、响应构造、NXDOMAIN构造
#pragma once

#include "../base/DnsString.h"
#include "../base/Vector.h"
#include <cstdint>
#include <cstring>

namespace dns {

class DnsProtocol {
public:
    // 从 DNS QNAME 段解码域名（处理标签长度前缀和压缩指针）
    // e.g. "\x03www\x05baidu\x03com\x00" → "www.baidu.com"
    static String decodeDomainName(const uint8_t* qname, int maxLen);

    // 从 DNS 响应中解析全部 A 记录 IPv4 地址
    // 返回 IP 字符串列表，无记录时返回空 Vector
    static Vector<String> parseARecords(const uint8_t* response, int len);

    // 构造 DNS 响应报文（从请求报文 + IP 列表）
    // resp 预分配 512 字节，返回实际长度（<0 表示失败）
    static int buildResponse(const uint8_t* req, int reqLen,
                             const Vector<String>& ips,
                             uint8_t* resp, int respSize);

    // 构造 NXDOMAIN 响应报文
    // resp 预分配 512 字节，返回实际长度
    static int buildNxdomain(const uint8_t* req, int reqLen,
                             uint8_t* resp, int respSize);

    // 从 DNS 报文提取 QTYPE（A=1, AAAA=28, etc.）
    static uint16_t extractQtype(const uint8_t* buffer, int len);
};

} // namespace dns
