<p align="center">
  <h1 align="center">dns-server-cpp</h1>
  <p align="center">
    <b>一个从零手写、零 STL 依赖的 C++ DNS 中继服务器</b><br/>
    容器 · 内存 · 并发 · 网络 · 协议 —— 全部亲手实现，只依赖 libc 与系统调用
  </p>
  <p align="center">
    <img alt="language" src="https://img.shields.io/badge/language-C%2B%2B17-00599C?style=flat-square&logo=c%2B%2B&logoColor=white">
    <img alt="stl" src="https://img.shields.io/badge/STL-Zero%20Dependency-red?style=flat-square">
    <img alt="platform" src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows-blue?style=flat-square">
    <img alt="build" src="https://img.shields.io/badge/build-CMake-brightgreen?style=flat-square&logo=cmake">
    <img alt="license" src="https://img.shields.io/badge/license-Personal%20Learning-lightgrey?style=flat-square">
  </p>
</p>

---

## 项目简介

`dns-server-cpp` 是一个**教学级但工业级结构**的 DNS 中继服务器。它把一条 DNS 查询链路从「UDP 收包」到「构造应答」完整拆解，并在此过程中**用 C++ 从零手写了几乎所有基础组件**——不引入任何 STL 容器、智能指针或第三方库。

这既是把「八股」变成「代码」的练习场，也是一份可以直接拿去面试讲解的**系统性作品集**：每一个组件背后，都对应着一个高频的底层知识点。

> ⚠️ **免责声明**：本项目为个人学习与面试展示用途，代码为手写练习实现，**未经过生产级验证**，请勿用于正式的 DNS 解析服务。

---

## ✨ 技术亮点

### 手写容器与内存管理
- **`Vector`** —— 三指针布局 + 二倍扩容 + placement new + 移动语义，复现 `std::vector` 的内存模型。
- **`DnsString`** —— 手写 `basic_string`，内置 **SSO（小字符串优化）**：14 字节内栈上存储，对 DNS 域名标签（通常 < 16 字节）命中率极高，大幅减少堆分配。
- **`LinkedHashMap`** —— 哈希桶 + 全局双向链表，**O(1) 查找 + LRU 淘汰**，复现 Java `LinkedHashMap` 语义。

### 手写智能指针
- **`UniquePtr` / `SharedPtr`** —— 引用计数、copy-and-swap、移动语义，完整复现 `shared_ptr` 的六大函数。

### 手写并发组件
- **`ThreadPool`** —— 核心线程 / 非核心线程、阻塞任务队列、`keepAlive` 超时回收、拒绝策略，完整复现 `ThreadPoolExecutor` 的线程模型。
- **`Logger`** —— 多生产者单消费者异步日志，有界队列满则丢弃（**业务线程永不阻塞**），未初始化时自动回退同步输出。

### 事件驱动网络
- **`Epoll`** —— Linux `epoll` / Windows `select` 跨平台事件循环封装。
- **`UdpSocket`** —— RAII 语义，自动管理 socket 生命周期与关闭。

### DNS 协议与中继
- **`DnsProtocol`** —— 手写 DNS 报文编解码：域名解析、A 记录提取、响应 / NXDOMAIN 构造。
- **`DnsRelay`** —— 持久上游 socket + Transaction ID 映射，还原客户端查询上下文。
- **二级缓存** —— 本地中继表（插入序）+ 上游响应缓存（LRU），命中即回，未命中透传上游。

---

## 🏗 架构

```
 客户端 ──UDP──▶  DnsServer (epoll 主循环)
                   ├─ serverSocket (UDP :53)  ← 接收查询
                   ├─ epoller                 ← 事件分发
                   ├─ threadPool (4/8)        ← 业务处理
                   ├─ localCache / dnsCache   ← 二级缓存
                   └─ relay (上游 socket)     ← 上游中继
                                               │
                                            上游 DNS (8.8.8.8)
```

**请求处理流程（A 记录）**

1. 主线程 `epoll_wait` 监听 `serverSocket` 可读 → 收取报文并提交线程池。
2. 工作线程解码域名、提取 QTYPE：
   - 命中 `localCache`（`dnsrelay.txt`）→ 直接构造响应（含 `0.0.0.0` 屏蔽）。
   - 命中 `dnsCache`（LRU）→ 直接构造响应。
   - 未命中 → `relay.submit` 中继到上游。
3. 上游响应到达 → 主线程通过 Transaction ID 映射找到原始查询，还原客户端 ID，回传客户端。

---

## 🚀 快速开始

### 依赖

| 依赖 | 版本 |
|------|------|
| CMake | ≥ 3.16 |
| 编译器 | 支持 C++17（Linux GCC / Clang，或 Windows MinGW / MSVC） |
| 系统库 | `Threads::Threads`（Windows 下 `ws2_32`） |

### 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 运行

```bash
./dns-server [选项] <上游DNS_IP> <中继表文件>

# 仅输出 error
./dns-server 8.8.8.8 dnsrelay.txt
# 额外输出 info
./dns-server -d 8.8.8.8 dnsrelay.txt
# 输出 debug
./dns-server -dd 8.8.8.8 dnsrelay.txt
```

### 中继表格式

`dnsrelay.txt` 每行一条 `IP 域名`，以 `#` 开头为注释；`IP=0.0.0.0` 表示屏蔽该域名：

```
# DNS 中继表样例
0.0.0.0 doubleclick.net    # 屏蔽广告域名
1.2.3.4 mytest.local        # 本地静态解析
```

---

## 🧩 模块全景

| 模块 | 路径 | 手写知识点 |
|------|------|-----------|
| `Vector` | `include/base/Vector.h` | 动态数组：三指针布局、扩容策略、移动语义、placement new |
| `DnsString` | `include/base/DnsString.h` | `basic_string` + SSO（14/15/16 字节边界） |
| `LinkedHashMap` | `include/base/LinkedHashMap.h` | 哈希表 + 双向链表 LRU（复现 Java `LinkedHashMap`） |
| `SmartPtr` | `include/base/SmartPtr.h` | `UniquePtr` / `SharedPtr`（拷贝 / 移动 / 引用计数） |
| `Allocator` / `Utility` | `include/base/` | 分配器、`move` / `forward` / `swap` 等基础工具 |
| `ThreadPool` | `include/ThreadPool.h` | 核心 / 非核心线程、工作队列、拒绝策略、keepalive 超时 |
| `Logger` | `include/Logger.h` | 多生产者单消费者异步日志（队满丢弃、优雅关闭） |
| `DnsProtocol` | `include/DnsProtocol.h` | DNS 报文编解码：域名解析、A 记录、响应 / NXDOMAIN |
| `DnsRelay` | `include/DnsRelay.h` | 持久上游 socket + Transaction ID 映射 + 超时清理 |
| `Socket` | `include/net/Socket.h` | RAII UDP socket（跨平台）、地址包装 |
| `Epoll` | `include/net/Epoll.h` | Linux `epoll` / Windows `select` 跨平台事件循环 |
| `DnsServer` | `include/DnsServer.h` | 主控：事件循环 + 线程池 + LRU 缓存 + 中继组装 |

---

## 📂 项目结构

```
.
├── include/
│   ├── base/            # 手写基础组件（Vector/String/LinkedHashMap/SmartPtr…）
│   ├── net/             # 网络封装（Epoll/Socket）
│   ├── Config.h         # 命令行配置解析
│   ├── DnsProtocol.h    # DNS 协议编解码
│   ├── DnsRelay.h       # DNS 中继管理器
│   ├── DnsServer.h      # 服务器主控
│   ├── Logger.h         # 异步日志
│   └── ThreadPool.h     # 线程池
├── src/                 # 各模块实现 + main.cpp
├── tests/               # 单元测试 + 压力测试
├── CMakeLists.txt
├── PROJECT.md           # 设计与实现说明（面试讲解用）
├── TEST_REPORT.md       # 完整测试报告
└── dnsrelay.txt         # DNS 中继表样例
```

---

## 🧪 测试

### 单元测试

覆盖各手写组件的关键行为，每个测试为独立可执行文件：

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build
```

| 测试 | 覆盖点 |
|------|--------|
| `test_vector` | 扩容 / 构造 / 移动 / 析构 |
| `test_string` | SSO 边界 / 拷贝移动 / append / c_str |
| `test_linked_hash_map` | 遍历序 / LRU / 容量淘汰 / 哈希冲突 |
| `test_thread_pool` | 核心线程 / 队列 / 扩容 / 拒绝 / 超时 / shutdown |
| `test_logger` | 异步 / 队满丢弃 / 关闭排空 / fallback |

### 压力测试

- **`stress_test`** —— 组件级压测，直测各手写组件的上限（扩容、缓存淘汰、线程池吞吐与拒绝、日志洪泛丢包）。
- **`stress_network`** —— 网络级压测，多线程并发 UDP 查询打满服务器，统计吞吐 / 成功率 / 超时 / 延迟。

```bash
# 组件级压测
./build/tests/stress_test

# 网络级压测（需先启动服务器）
./build/dns-server 8.8.8.8 dnsrelay.txt &
./build/tests/stress_network <并发线程数> <每线程查询数> <域名>
```

> 完整测试数据见 [`TEST_REPORT.md`](TEST_REPORT.md)，设计取舍与线程模型见 [`PROJECT.md`](PROJECT.md)。

---

## 🗺 Roadmap

- [ ] 接入 SIGINT / SIGTERM 信号处理的优雅退出（`DnsServer::stop()` 已提供）
- [ ] DNS 压缩指针（`0xC0`）的完整展开，支持更复杂的上游响应
- [ ] 硬编码常量配置化（线程池队列、查询 ID 映射表容量）
- [ ] 缓存过期（TTL）机制与淘汰统计
- [ ] 接入 benchmark 与 CI

## 已知限制

- 服务器暂未接入信号处理的优雅退出。
- 中继 socket 依赖首次 `sendto` 自动绑定临时端口，`DnsRelay::init` 未显式 bind 与校验上游地址。
- 域名解码对 DNS 压缩指针（`0xC0`）仅跳过不展开，复杂上游响应解析有限。
- Windows 下用 `select` 替代 `epoll`，性能略低。
- 部分容量为硬编码常量（线程池队列 50、查询 ID 映射表 128）。

---

## 📄 License

本项目为个人学习与面试准备用途，未指定开源协议。
