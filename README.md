# dns-server-cpp

一个**零 STL、全手写**的 C++ DNS 中继服务器。

> ⚠️ **个人练习项目**：本项目仅用于学习 C++ 底层组件（容器 / 内存管理 / 并发 / 网络 / 协议），
> **不面向生产环境**，请勿用于正式 DNS 解析服务。

## 练习内容

| 模块 | 练习点 |
|------|--------|
| `base/Vector` | 手写动态数组：三指针布局、扩容策略、移动语义、placement new |
| `base/DnsString` | 手写 `basic_string` + SSO（小字符串优化，14/15/16 字节边界） |
| `base/LinkedHashMap` | 哈希表 + 双向链表的 LRU 缓存（模拟 Java `LinkedHashMap`） |
| `base/SmartPtr` | 手写 `UniquePtr` / `SharedPtr`（拷贝/移动/引用计数） |
| `base/Allocator` / `Utility` | 分配器、`move` / `forward` / `swap` 等基础工具 |
| `ThreadPool` | 手写线程池（核心/非核心线程、工作队列、拒绝策略、keepalive 超时） |
| `Logger` | 多生产者单消费者异步日志（队满丢弃、优雅关闭） |
| `DnsProtocol` | DNS 报文编解码：域名解析、A 记录、响应 / NXDOMAIN 构造 |
| `DnsRelay` | 持久上游 socket + Transaction ID 映射 + 超时清理 |
| `net/Socket` | RAII UDP socket（跨平台）、地址包装 |
| `net/Epoll` | Linux `epoll` / Windows `select` 跨平台事件循环 |
| `DnsServer` | 主控：事件循环 + 线程池 + LRU 缓存 + DNS 中继组装 |

## 目录结构

```
.
├── include/
│   ├── base/          # 手写基础组件（Vector/String/LinkedHashMap/SmartPtr…）
│   ├── net/           # 网络封装（Epoll/Socket）
│   ├── Config.h       # 命令行配置解析
│   ├── DnsProtocol.h  # DNS 协议编解码
│   ├── DnsRelay.h     # DNS 中继管理器
│   ├── DnsServer.h    # 服务器主控
│   ├── Logger.h       # 异步日志
│   └── ThreadPool.h   # 线程池
├── src/               # 各模块实现 + main.cpp
├── tests/             # 单元测试 + 压力测试
├── CMakeLists.txt
└── dnsrelay.txt       # DNS 中继表样例
```

## 编译运行

依赖：CMake ≥ 3.16、支持 C++17 的编译器（Linux GCC/Clang 或 Windows MinGW/MSVC）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

启动（默认监听 53 端口）：

```bash
./dns-server [选项] <上游DNS_IP> <中继表文件>
# 例如：
./dns-server 8.8.8.8 dnsrelay.txt     # 仅输出 error
./dns-server -d 8.8.8.8 dnsrelay.txt  # 额外输出 info
./dns-server -dd 8.8.8.8 dnsrelay.txt # 输出 debug
```

中继表 `dnsrelay.txt` 每行一条 `IP 域名`，`IP=0.0.0.0` 表示屏蔽该域名。

## 测试

### 单元测试（AI 辅助制作）

> 单元测试用例由 AI 辅助编写，覆盖各手写组件的关键行为。

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build
```

各测试为独立可执行文件，位于 `tests/`：

| 测试 | 覆盖点 |
|------|--------|
| `test_vector` | 扩容 / 构造 / 移动 / 析构 |
| `test_string` | SSO 边界 / 拷贝移动 / append / c_str |
| `test_linked_hash_map` | 遍历序 / LRU / 容量淘汰 / 哈希冲突 |
| `test_thread_pool` | 核心线程 / 队列 / 扩容 / 拒绝 / 超时 / shutdown |
| `test_logger` | 异步 / 队满丢弃 / 关闭排空 / fallback |

### 压力测试

- `stress_test`：**组件级**压测，直测各手写组件的上限（扩容、缓存淘汰、线程池吞吐与拒绝、日志洪泛丢包）。
- `stress_network`：**网络级**压测，多线程并发 UDP 查询打满服务器，统计吞吐 / 成功率 / 超时 / 延迟。

```bash
# 组件级压测
./build/tests/stress_test

# 网络级压测（需先启动服务器）
./build/dns-server 8.8.8.8 dnsrelay.txt &
./build/tests/stress_network <并发线程数> <每线程查询数> <域名>
```

## 免责声明

本仓库为个人学习与面试准备用途，代码为手写练习实现，未经过生产级验证。
