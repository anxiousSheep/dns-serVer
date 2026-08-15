// SmartPtr.h — 手写 UniquePtr + SharedPtr
// 柠檬微趣面试高频手撕题：shared_ptr 六个函数
#pragma once

#include "Utility.h"
#include <cstddef>

namespace dns {

// ==================== DefaultDeleter ====================
template<typename T>
struct DefaultDeleter {
    void operator()(T* p) const { delete p; }
};

// 数组特化
template<typename T>
struct DefaultDeleter<T[]> {
    void operator()(T* p) const { delete[] p; }
};

// ==================== UniquePtr ====================
template<typename T, typename Deleter = DefaultDeleter<T>>
class UniquePtr {
    T*      ptr;
    Deleter deleter;

public:
    UniquePtr() noexcept : ptr(nullptr), deleter() {}
    explicit UniquePtr(T* p) noexcept : ptr(p), deleter() {}

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& other) noexcept
        : ptr(other.ptr), deleter(move(other.deleter)) {
        other.ptr = nullptr;
    }

    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            reset(other.release());
            deleter = move(other.deleter);
        }
        return *this;
    }

    ~UniquePtr() {
        if (ptr) deleter(ptr);
    }

    T*   get()        const noexcept { return ptr; }
    T&   operator*()  const { return *ptr; }
    T*   operator->() const noexcept { return ptr; }

    T* release() noexcept {
        T* p = ptr;
        ptr = nullptr;
        return p;
    }

    void reset(T* p = nullptr) noexcept {
        T* old = ptr;
        ptr = p;
        if (old) deleter(old);
    }

    explicit operator bool() const noexcept { return ptr != nullptr; }

    void swap(UniquePtr& other) noexcept {
        dns::swap(ptr, other.ptr);
        dns::swap(deleter, other.deleter);
    }
};

// ==================== SharedPtr ====================
template<typename T>
class SharedPtr {
    T*   ptr;
    int* refCount;   // 堆上分配，多个 SharedPtr 共享同一块计数

public:
    // 1. 默认构造
    SharedPtr() noexcept : ptr(nullptr), refCount(nullptr) {}

    // 2. 有参构造
    explicit SharedPtr(T* p) : ptr(p), refCount(new int(1)) {}

    // 3. 拷贝构造
    SharedPtr(const SharedPtr& other) noexcept
        : ptr(other.ptr), refCount(other.refCount) {
        if (refCount) ++(*refCount);
    }

    // 4. 移动构造
    SharedPtr(SharedPtr&& other) noexcept
        : ptr(other.ptr), refCount(other.refCount) {
        other.ptr = nullptr;
        other.refCount = nullptr;
    }

    // 5. 拷贝赋值 (copy-and-swap, 隐含移动赋值)
    SharedPtr& operator=(SharedPtr other) noexcept {
        _M_swap(other);
        return *this;
    }

    // 6. 析构
    ~SharedPtr() {
        _M_release();
    }

    T&   operator*()  const { return *ptr; }
    T*   operator->() const noexcept { return ptr; }
    T*   get()        const noexcept { return ptr; }

    int  use_count() const noexcept {
        return refCount ? *refCount : 0;
    }

    explicit operator bool() const noexcept { return ptr != nullptr; }

    void reset(T* p = nullptr) {
        _M_release();
        ptr = p;
        refCount = p ? new int(1) : nullptr;
    }

    void swap(SharedPtr& other) noexcept { _M_swap(other); }

private:
    void _M_release() {
        if (refCount) {
            --(*refCount);
            if (*refCount == 0) {
                delete ptr;
                delete refCount;
            }
        }
        ptr = nullptr;
        refCount = nullptr;
    }

    void _M_swap(SharedPtr& other) noexcept {
        dns::swap(ptr, other.ptr);
        dns::swap(refCount, other.refCount);
    }
};

// make_unique / make_shared
template<typename T, typename... Args>
UniquePtr<T> makeUnique(Args&&... args) {
    return UniquePtr<T>(new T(forward<Args>(args)...));
}

template<typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
    return SharedPtr<T>(new T(forward<Args>(args)...));
}

} // namespace dns
