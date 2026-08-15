cmake_minimum_required(VERSION 3.16)
project(DnsServerCpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 源文件
set(SOURCES
    src/main.cpp
    src/ThreadPool.cpp
    src/Logger.cpp
    src/DnsProtocol.cpp
    src/DnsRelay.cpp
    src/Config.cpp
    src/DnsServer.cpp
)

# 头文件目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/base
    ${CMAKE_SOURCE_DIR}/include/net
)

# 可执行文件
add_executable(dns-server ${SOURCES})

# 链接库
find_package(Threads REQUIRED)
target_link_libraries(dns-server PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(dns-server PRIVATE ws2_32)
endif()

# 测试（可选）
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
