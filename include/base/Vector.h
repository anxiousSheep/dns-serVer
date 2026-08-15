// Vector.h — 手写 vector，复现 STL 设计
// _Vector_base 持有 allocator + 三指针，vector 通过 protected 继承获得 EBCO 优化
#pragma once

#include "Allocator.h"
#include "Utility.h"
#include "Iterator.h"

namespace dns {

// ==================== _Vector_base ====================
// 管理原始内存的分配/释放，不关心对象构造/析构
template<typename T, typename Alloc = Allocator<T>>
class _Vector_base {
public:
    using allocator_type = Alloc;

    // _Vector_impl 继承 Alloc — 利用 EBCO，无状态分配器不占空间
    struct _Vector_impl : public Alloc {
        T* start;           // 数组首元素
        T* finish;          // 尾元素 + 1
        T* end_of_storage;  // 容量末尾 + 1

        _Vector_impl() noexcept
            : Alloc(), start(nullptr), finish(nullptr), end_of_storage(nullptr) {}

        _Vector_impl(const Alloc& a) noexcept
            : Alloc(a), start(nullptr), finish(nullptr), end_of_storage(nullptr) {}

        // 交换所有状态（含分配器，通常分配器无状态所以是 no-op）
        void _M_swap_data(_Vector_impl& other) noexcept {
            dns::swap(start, other.start);
            dns::swap(finish, other.finish);
            dns::swap(end_of_storage, other.end_of_storage);
        }
    };

protected:
    _Vector_impl impl;

    // 分配 n 个 T 的原始内存
    T* _M_allocate(size_t n) {
        return impl.allocate(n);
    }

    // 归还 n 个 T 的原始内存
    void _M_deallocate(T* p, size_t n) {
        if (p) impl.deallocate(p, n);
    }

public:
    _Vector_base() noexcept = default;

    explicit _Vector_base(const Alloc& a) noexcept : impl(a) {}
    _Vector_base(size_t n, const Alloc& a = Alloc())
        : impl(a) {
        impl.start = _M_allocate(n);
        impl.finish = impl.start;
        impl.end_of_storage = impl.start + n;
    }

    ~_Vector_base() {
        _M_deallocate(impl.start, capacity());
    }

    // 禁止拷贝，允许移动
    _Vector_base(const _Vector_base&) = delete;
    _Vector_base& operator=(const _Vector_base&) = delete;

    _Vector_base(_Vector_base&& other) noexcept
        : impl(move(other.impl)) {
        other.impl.start = nullptr;
        other.impl.finish = nullptr;
        other.impl.end_of_storage = nullptr;
    }

    size_t capacity() const noexcept {
        if (!impl.start) return 0;
        return static_cast<size_t>(impl.end_of_storage - impl.start);
    }

    Alloc get_allocator() const noexcept {
        return static_cast<const Alloc&>(impl);
    }
};

// ==================== vector ====================
template<typename T, typename Alloc = Allocator<T>>
class Vector : protected _Vector_base<T, Alloc> {
    using _Base = _Vector_base<T, Alloc>;
    using _Base::impl;
    using _Base::_M_allocate;
    using _Base::_M_deallocate;

public:
    // ---- 类型别名 ----
    using value_type        = T;
    using pointer           = T*;
    using const_pointer     = const T*;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = size_t;
    using difference_type   = ptrdiff_t;
    using iterator          = T*;
    using const_iterator    = const T*;
    using reverse_iterator  = ReverseIterator<iterator>;
    using allocator_type    = Alloc;

    // ---- 构造/析构 ----
    Vector() noexcept = default;

    explicit Vector(size_t count, const T& val = T())
        : _Base(count) {
        _M_default_initialize_n(impl.start, count, val);
        impl.finish = impl.start + count;
    }

    Vector(const Vector& other)
        : _Base(other.size()) {
        _M_copy_initialize(other.impl.start, other.impl.finish);
        impl.finish = impl.start + other.size();
    }

    Vector(Vector&& other) noexcept
        : _Base(move(other)) {
        // _Base 移动构造已经正确移动了三指针并重置了 other
    }

    ~Vector() {
        _M_destroy_range(impl.start, impl.finish);
    }

    Vector& operator=(Vector other) {
        swap(other);
        return *this;
    }

    // ---- 元素访问 ----
    T& operator[](size_t i)             { return impl.start[i]; }
    const T& operator[](size_t i) const { return impl.start[i]; }
    T& front()                          { return *impl.start; }
    T& back()                           { return *(impl.finish - 1); }
    T* data()                           { return impl.start; }
    const T* data() const               { return impl.start; }

    // ---- 容量 ----
    size_t size()     const noexcept { if (!impl.start) return 0; return static_cast<size_t>(impl.finish - impl.start); }
    size_t capacity() const noexcept { return _Base::capacity(); }
    bool   empty()    const noexcept { return impl.finish == impl.start; }

    // ---- 迭代器 ----
    iterator       begin()        { return impl.start; }
    const_iterator begin()  const { return impl.start; }
    iterator       end()          { return impl.finish; }
    const_iterator end()    const { return impl.finish; }

    // ---- 修改 ----
    void push_back(const T& val) {
        if (impl.finish == impl.end_of_storage)
            _M_realloc(capacity() == 0 ? 1 : capacity() * 2);
        impl.construct(impl.finish, val);
        ++impl.finish;
    }

    void push_back(T&& val) {
        if (impl.finish == impl.end_of_storage)
            _M_realloc(capacity() == 0 ? 1 : capacity() * 2);
        impl.construct(impl.finish, move(val));
        ++impl.finish;
    }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        if (impl.finish == impl.end_of_storage)
            _M_realloc(capacity() == 0 ? 1 : capacity() * 2);
        ::new(static_cast<void*>(impl.finish)) T(forward<Args>(args)...);
        ++impl.finish;
    }

    void pop_back() {
        if (impl.finish != impl.start) {
            --impl.finish;
            impl.destroy(impl.finish);
        }
    }

    void clear() {
        _M_destroy_range(impl.start, impl.finish);
        impl.finish = impl.start;
    }

    void reserve(size_t newCap) {
        if (newCap > capacity())
            _M_realloc(newCap);
    }

    // ---- swap ----
    void swap(Vector& other) noexcept {
        impl._M_swap_data(other.impl);
    }

    // ---- 分配器 ----
    Alloc get_allocator() const noexcept { return _Base::get_allocator(); }

private:
    // 扩容核心：分配新内存 → 移动元素 → 归还旧内存
    void _M_realloc(size_t newCap) {
        T* newStart = _M_allocate(newCap);
        T* newFinish = newStart;

        // 尝试移动旧元素到新内存
        for (T* p = impl.start; p != impl.finish; ++p) {
            impl.construct(newFinish, move(*p));
            ++newFinish;
        }

        // 析构旧元素
        _M_destroy_range(impl.start, impl.finish);

        // 归还旧内存
        size_t oldCap = capacity();
        _M_deallocate(impl.start, oldCap);

        // 更新三指针
        impl.start = newStart;
        impl.finish = newFinish;
        impl.end_of_storage = newStart + newCap;
    }

    // 析构 [first, last) 区间的元素
    void _M_destroy_range(T* first, T* last) {
        while (first != last) {
            impl.destroy(first);
            ++first;
        }
    }

    // 从 [src, src+size()) 拷贝构造到 [dst, ...)
    void _M_copy_initialize(T* src, T* srcEnd) {
        T* dst = impl.start;
        while (src != srcEnd) {
            impl.construct(dst, *src);
            ++dst;
            ++src;
        }
    }

    // 用 val 填充 [dst, dst+n)
    void _M_default_initialize_n(T* dst, size_t n, const T& val) {
        for (size_t i = 0; i < n; ++i) {
            impl.construct(dst + i, val);
        }
    }
};

} // namespace dns
