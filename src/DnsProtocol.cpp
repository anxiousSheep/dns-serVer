// DnsProtocol.cpp — DNS 协议编解码实现
#include "../include/DnsProtocol.h"
#include "../include/Logger.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>  // ntohs, htons, htonl
#endif
#include <cstdio>

namespace dns {

String DnsProtocol::decodeDomainName(const uint8_t* qname, int maxLen) {
    if (!qname || maxLen <= 0) return String("");

    char out[256];
    int  outLen = 0;
    int  offset = 0;

    while (offset < maxLen) {
        uint8_t labelLen = qname[offset];
        if (labelLen == 0) break;              // 根标签（结束）
        if ((labelLen & 0xC0) == 0xC0) break;  // 压缩指针，不展开

        offset++;
        if (offset + labelLen > maxLen) break;

        for (int i = 0; i < labelLen; i++) {
            if (outLen < 254) out[outLen++] = (char)qname[offset++];
        }
        if (outLen < 254) out[outLen++] = '.';
    }

    if (outLen > 0 && out[outLen - 1] == '.')
        out[outLen - 1] = '\0';  // 去掉末尾点
    else
        out[outLen] = '\0';

    return String(out);
}

Vector<String> DnsProtocol::parseARecords(const uint8_t* response, int len) {
    Vector<String> result;
    if (len < 12) return result;

    uint16_t flags   = ntohs(*(uint16_t*)(response + 2));
    uint16_t qdcount = ntohs(*(uint16_t*)(response + 4));
    uint16_t ancount = ntohs(*(uint16_t*)(response + 6));

    if ((flags & 0x8000) == 0) return result;  // 非响应
    if ((flags & 0x000F) != 0) return result;  // RCODE 异常
    if (ancount == 0)           return result;

    // 跳过 Question Section
    int offset = 12;
    for (int i = 0; i < qdcount; i++) {
        while (offset < len) {
            uint8_t c = response[offset];
            if (c == 0)          { offset++;  break; }
            if ((c & 0xC0) == 0xC0) { offset += 2; break; }
            offset += c + 1;
        }
        offset += 4;  // QTYPE + QCLASS
    }

    // 遍历 Answer Section
    for (int i = 0; i < ancount && offset < len; i++) {
        // Name
        if ((response[offset] & 0xC0) == 0xC0) {
            offset += 2;
        } else {
            while (offset < len && response[offset] != 0)
                offset += (uint8_t)response[offset] + 1;
            offset++;  // 跳 0x00
        }

        if (offset + 10 > len) break;

        uint16_t type     = ntohs(*(uint16_t*)(response + offset)); offset += 2;
        uint16_t qclass   = ntohs(*(uint16_t*)(response + offset)); offset += 2;
        offset += 4;  // TTL
        uint16_t rdlength = ntohs(*(uint16_t*)(response + offset)); offset += 2;

        if (type == 1 && qclass == 1 && rdlength == 4 && offset + 4 <= len) {
            uint8_t* ip = (uint8_t*)(response + offset);
            char ipStr[32];
            snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            result.push_back(String(ipStr));
        }
        offset += rdlength;
    }

    return result;
}

int DnsProtocol::buildResponse(const uint8_t* req, int reqLen,
                                const Vector<String>& ips,
                                uint8_t* resp, int respSize) {
    if (respSize < 512 || reqLen < 12 || ips.empty()) return -1;

    memcpy(resp, req, reqLen);

    // Flags: QR=1, RD=1, RA=1, RCODE=0
    *(uint16_t*)(resp + 2) = htons(0x8180);
    // ANCOUNT = ips.size()
    *(uint16_t*)(resp + 6) = htons((uint16_t)ips.size());

    int offset = reqLen;
    for (size_t i = 0; i < ips.size(); i++) {
        if (offset + 16 > respSize) break;

        // Name: 压缩指针 0xC00C（指向报文偏移 12 处的原始域名）
        resp[offset++] = 0xC0;
        resp[offset++] = 0x0C;

        // TYPE=A
        *(uint16_t*)(resp + offset) = htons(1); offset += 2;
        // CLASS=IN
        *(uint16_t*)(resp + offset) = htons(1); offset += 2;
        // TTL=300
        *(uint32_t*)(resp + offset) = htonl(300); offset += 4;
        // RDLENGTH=4
        *(uint16_t*)(resp + offset) = htons(4); offset += 2;

        // RDATA: IPv4
        unsigned int a, b, c, d;
        sscanf(ips[i].c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
        resp[offset++] = (uint8_t)a;
        resp[offset++] = (uint8_t)b;
        resp[offset++] = (uint8_t)c;
        resp[offset++] = (uint8_t)d;
    }

    return offset;
}

int DnsProtocol::buildNxdomain(const uint8_t* req, int reqLen,
                                uint8_t* resp, int respSize) {
    if (respSize < 512 || reqLen < 12) return -1;

    memcpy(resp, req, reqLen);
    // Flags: QR=1, RCODE=3 (NXDOMAIN)
    *(uint16_t*)(resp + 2) = htons(0x8183);
    *(uint16_t*)(resp + 6) = 0;  // ANCOUNT=0

    return reqLen;
}

uint16_t DnsProtocol::extractQtype(const uint8_t* buffer, int len) {
    if (len < 12) return 0;

    // 遍历 QNAME 找到 Question Section 的 QTYPE
    int offset = 12;
    while (offset < len) {
        uint8_t c = buffer[offset];
        if (c == 0)          { offset++; break; }
        if ((c & 0xC0) == 0xC0) { offset += 2; break; }
        offset += c + 1;
    }
    if (offset + 2 > len) return 0;

    return ntohs(*(uint16_t*)(buffer + offset));
}

} // namespace dns
