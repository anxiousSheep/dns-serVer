// Allocator.h — 简易分配器，与 STL allocator 接口兼容
// 直接封装 ::operator new/delete，不实现内存池
#pragma once

#include <cstddef>   // size_t
#include <new>       // placement new

namespace dns {

template<typename T>
class Allocator {
public:
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;

    // rebind: 允许从 Allocator<T> 获得 Allocator<U>
    template<typename U>
    struct rebind { using other = Allocator<U>; };

    Allocator() noexcept = default;
    Allocator(const Allocator&) noexcept = default;
    template<typename U>
    Allocator(const Allocator<U>&) noexcept {}

    // 分配 n 个 T 的原始内存（不构造对象）
    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    // 归还 n 个 T 的原始内存
    void deallocate(T* p, size_t /*n*/) {
        ::operator delete(p);
    }

    // placement new 构造对象
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new(static_cast<void*>(p)) U(args...);
    }

    // 调用析构函数
    template<typename U>
    void destroy(U* p) {
        p->~U();
    }
};

// Allocator 相等比较（所有 Allocator 实例等价）
template<typename T, typename U>
bool operator==(const Allocator<T>&, const Allocator<U>&) noexcept { return true; }

template<typename T, typename U>
bool operator!=(const Allocator<T>&, const Allocator<U>&) noexcept { return false; }

} // namespace dns
