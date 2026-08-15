# 单元测试与压力测试（BUILD_TESTS=ON 时构建）
# 每个测试为独立可执行文件；依赖服务器源码的测试直接编译对应 .cpp

# 头文件目录（顶层已配置，此处显式声明以保证本目录自包含）
include_directories(${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_SOURCE_DIR}/include/base
                    ${CMAKE_SOURCE_DIR}/include/net)

# ---- 单元测试（AI 辅助制作）----
add_executable(test_vector test_vector.cpp)
add_executable(test_string test_string.cpp)
add_executable(test_linked_hash_map test_linked_hash_map.cpp)
add_executable(test_thread_pool test_thread_pool.cpp ../src/ThreadPool.cpp)
add_executable(test_logger test_logger.cpp ../src/Logger.cpp ../src/ThreadPool.cpp)

# ---- 压力测试 ----
add_executable(stress_test stress_test.cpp ../src/ThreadPool.cpp ../src/Logger.cpp)
add_executable(stress_network stress_network.cpp)

# ---- 链接 pthread / 网络库 ----
set(TEST_TARGETS
    test_vector test_string test_linked_hash_map
    test_thread_pool test_logger stress_test stress_network)

foreach(t ${TEST_TARGETS})
    target_link_libraries(${t} PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(${t} PRIVATE ws2_32)
    endif()
endforeach()
