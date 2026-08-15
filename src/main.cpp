// main.cpp — DNS Server entry point
// Usage: ./dns-server [-d|-dd] <upstream_dns_ip> <dnsrelay_file>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <cstdio>
#include "../include/Config.h"
#include "../include/DnsServer.h"
#include "../include/Logger.h"

int main(int argc, char* argv[]) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    // 解析配置
    dns::Config config;
    if (!dns::Config::parse(argc, argv, config)) {
        fprintf(stderr, "Usage: %s [-d|-dd] <upstream_dns_ip> <dnsrelay_file>\n", argv[0]);
        return 1;
    }

    // 启动服务器
    dns::DnsServer server(config);
    server.run();

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
