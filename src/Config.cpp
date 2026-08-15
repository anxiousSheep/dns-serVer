// Config.cpp — 命令行参数解析与 DNS 中继表加载
#include "../include/Config.h"
#include "../base/LinkedHashMap.h"
#include "../base/Vector.h"
#include "../base/DnsString.h"
#include "../include/Logger.h"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace dns {

// ---- IPv4 校验 ----
static bool isValidIp(const char* str) {
    int dots = 0;
    const char* p = str;
    if (*p == '\0') return false;

    while (*p) {
        if (!isdigit((unsigned char)*p)) return false;

        int num = 0;
        int digits = 0;
        while (isdigit((unsigned char)*p)) {
            num = num * 10 + (*p - '0');
            digits++;
            if (digits > 3) return false;
            p++;
        }
        if (num > 255) return false;

        if (*p == '.') {
            dots++;
            p++;
            if (!isdigit((unsigned char)*p)) return false;
        } else if (*p == '\0') {
            break;
        } else {
            return false;
        }
    }
    return dots == 3;
}

// ---- 文件名校验 ----
static bool isValidFilename(const char* str) {
    size_t len = strlen(str);
    if (len < 5) return false;
    const char* ext = str + len - 4;
    return (strcmp(ext, ".txt") == 0 || strcmp(ext, ".csv") == 0);
}

// ---- 解析参数 ----
bool Config::parse(int argc, char* argv[], Config& out) {
    bool hasDebug = false;
    bool hasIp    = false;
    bool hasFile  = false;

    Logger::info("Program started with %d argument(s):", argc);
    for (int i = 0; i < argc; i++) {
        Logger::info("  argv[%d] = %s", i, argv[i]);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            if (hasDebug) {
                Logger::error("Duplicate debug flag");
                return false;
            }
            out.debugLevel = 1;
            hasDebug = true;
        } else if (strcmp(argv[i], "-dd") == 0) {
            if (hasDebug) {
                Logger::error("Duplicate debug flag");
                return false;
            }
            out.debugLevel = 2;
            hasDebug = true;
        } else if (isValidIp(argv[i])) {
            if (hasIp) {
                Logger::error("Duplicate DNS server IP '%s'", argv[i]);
                return false;
            }
            out.dnsServer = String(argv[i]);
            hasIp = true;
        } else if (isValidFilename(argv[i])) {
            if (hasFile) {
                Logger::error("Duplicate config filename '%s'", argv[i]);
                return false;
            }
            out.filename = String(argv[i]);
            hasFile = true;
        } else {
            Logger::error("Unknown or invalid argument '%s'", argv[i]);
            return false;
        }
    }

    Logger::info("Parsed configuration:");
    Logger::info("  Debug Level : %d", out.debugLevel);
    Logger::info("  DNS Server  : %s", out.dnsServer.c_str());
    Logger::info("  Config File : %s", out.filename.c_str());

    return true;
}

// ---- 加载 DNS 中继表 ----
template<typename Map>
bool loadDnsRelayTable(const char* filepath, Map& cache) {
    if (!filepath) return false;

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        Logger::error("Failed to open DNS relay table: %s", filepath);
        return false;
    }

    char line[256];
    int  count  = 0;
    int  lineNo = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineNo++;
        // 去换行符
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0' || line[0] == '#') continue;

        char ipStr[32];
        char domain[128];
        if (sscanf(line, "%31s %127s", ipStr, domain) == 2) {
            String domainKey(domain);
            Vector<String>* existing = cache.get(domainKey);
            if (existing) {
                existing->push_back(String(ipStr));
            } else {
                Vector<String> newList;
                newList.push_back(String(ipStr));
                cache.put(domainKey, newList);
            }
            count++;
        }
    }

    fclose(fp);
    Logger::info("Loaded %d entries from '%s' into local cache", count, filepath);
    return true;
}

// 显式实例化（在 DnsServer 构造中直接调用，编译期自动展开）
template bool loadDnsRelayTable(const char*, LinkedHashMap<String, Vector<String>>&);

} // namespace dns
