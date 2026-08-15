// Config.h — 命令行配置解析
// 解析参数：[-d|-dd] <upstream_dns_ip> <dnsrelay_file>
#pragma once

#include "../base/DnsString.h"

namespace dns {

struct Config {
    String dnsServer;    // 上游 DNS IP，默认 "8.8.8.8"
    String filename;     // 中继表文件，默认 "dnsrelay.txt"
    int    debugLevel;   // 0=none, 1=info, 2=debug

    Config() : dnsServer("8.8.8.8"), filename("dnsrelay.txt"), debugLevel(0) {}

    // 解析命令行参数，成功返回 true
    static bool parse(int argc, char* argv[], Config& out);
};

// 加载 dnsrelay.txt 到 LinkedHashMap
// 文件格式：每行 "IP 域名"，IP=0.0.0.0 表示屏蔽
template<typename Map>
bool loadDnsRelayTable(const char* filepath, Map& cache);

} // namespace dns
