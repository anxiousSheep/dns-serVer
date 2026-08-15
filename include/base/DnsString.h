// String.h — 手写 basic_string，含 SSO（Small String Optimization）
// DNS 场景下 SSO 命中率 > 90%（域名标签通常 < 16 字节）
#pragma once

#include "Allocator.h"
#include "Utility.h"
#include <cstring>   // memcpy, memmove, strlen, strcmp

namespace dns {

template<typename CharT = char, typename Alloc = Allocator<CharT>>
class basic_string {
public:
    using value_type      = CharT;
    using pointer         = CharT*;
    using const_pointer   = const CharT*;
    using reference       = CharT&;
    using const_reference = const CharT&;
    using size_type       = size_t;
    using iterator        = CharT*;
    using const_iterator  = const CharT*;

    static constexpr size_t SSO_CAP = 14;   // 14 char + '\0' + flag byte = 16 bytes

    // ---- 构造/析构 ----
    basic_string() noexcept {
        _M_init_short();
    }

    basic_string(const CharT* s) {
        size_t len = _M_strlen(s);
        if (len <= SSO_CAP) {
            _M_init_short();
            _M_copy_chars(short_.buf, s, len);
            _M_set_short_size(len);
        } else {
            long_.capacity = len;
            long_.size = len;
            long_.ptr = _M_alloc_long(len + 1);
            _M_copy_chars(long_.ptr, s, len);
            long_.ptr[len] = '\0';
        }
    }

    basic_string(const basic_string& other) {
        if (other._M_is_short()) {
            _M_init_short();
            size_t sz = other._M_short_size();
            _M_copy_chars(short_.buf, other.short_.buf, sz);
            _M_set_short_size(sz);
        } else {
            long_.capacity = other.long_.capacity;
            long_.size = other.long_.size;
            long_.ptr = _M_alloc_long(other.long_.capacity + 1);
            _M_copy_chars(long_.ptr, other.long_.ptr, other.long_.size);
            long_.ptr[other.long_.size] = '\0';
        }
    }

    basic_string(basic_string&& other) noexcept {
        if (other._M_is_short()) {
            _M_copy_chars(short_.buf, other.short_.buf, 16);
        } else {
            long_.ptr = other.long_.ptr;
            long_.size = other.long_.size;
            long_.capacity = other.long_.capacity;
        }
        other._M_init_short();
    }

    ~basic_string() {
        if (!_M_is_short()) {
            ::operator delete(long_.ptr);
        }
    }

    // ---- 赋值 ----
    basic_string& operator=(const basic_string& other) {
        if (this != &other) {
            basic_string tmp(other);
            swap(tmp);
        }
        return *this;
    }

    basic_string& operator=(const CharT* s) {
        basic_string tmp(s);
        swap(tmp);
        return *this;
    }

    // ---- 访问 ----
    const CharT* c_str() const {
        return _M_is_short() ? short_.buf : long_.ptr;
    }

    const CharT* data()  const { return c_str(); }
    CharT*       data()        { return _M_is_short() ? short_.buf : long_.ptr; }

    size_t length() const { return _M_is_short() ? _M_short_size() : long_.size; }
    size_t size()   const { return length(); }
    bool   empty()  const { return length() == 0; }

    CharT& operator[](size_t i)       { return data()[i]; }
    CharT  operator[](size_t i) const { return data()[i]; }

    iterator begin()       { return data(); }
    iterator end()         { return data() + length(); }
    const_iterator begin() const { return data(); }
    const_iterator end()   const { return data() + length(); }

    // ---- 比较 ----
    bool operator==(const basic_string& other) const {
        if (length() != other.length()) return false;
        return _M_strcmp(c_str(), other.c_str()) == 0;
    }

    bool operator==(const CharT* s) const {
        return _M_strcmp(c_str(), s) == 0;
    }

    bool operator!=(const basic_string& other) const { return !(*this == other); }
    bool operator!=(const CharT* s)            const { return !(*this == s); }

    // ---- 操作 ----
    void clear() {
        if (_M_is_short()) {
            short_.buf[0] = '\0';
            _M_set_short_size(0);
        } else {
            long_.ptr[0] = '\0';
            long_.size = 0;
        }
    }

    basic_string& append(const CharT* s) {
        return append(s, _M_strlen(s));
    }

    basic_string& append(const CharT* s, size_t n) {
        if (n == 0) return *this;
        size_t oldLen = length();
        size_t newLen = oldLen + n;
        if (newLen > capacity_impl()) {
            _M_grow(newLen);
        }
        CharT* dst = data() + oldLen;
        _M_copy_chars(dst, s, n);
        dst[n] = '\0';
        _M_set_size(newLen);
        return *this;
    }

    basic_string& operator+=(const CharT* s) { return append(s); }
    basic_string& operator+=(const basic_string& s) { return append(s.c_str(), s.length()); }

    void reserve(size_t newCap) {
        if (newCap > capacity_impl()) _M_grow(newCap);
    }

    void swap(basic_string& other) noexcept {
        // 简单 swap: 交换整个 union (最大成员拷来拷去)
        CharT tmpBuf[sizeof(_Long)];
        // 拷贝自己的状态到 tmp
        if (_M_is_short()) {
            _M_copy_chars(reinterpret_cast<CharT*>(tmpBuf), short_.buf, 16);
        } else {
            _M_copy_chars(reinterpret_cast<CharT*>(tmpBuf),
                          reinterpret_cast<CharT*>(&long_), sizeof(_Long));
        }
        // other → this
        if (other._M_is_short()) {
            _M_copy_chars(short_.buf, other.short_.buf, 16);
        } else {
            _M_copy_chars(reinterpret_cast<CharT*>(&long_),
                          reinterpret_cast<CharT*>(&other.long_), sizeof(_Long));
        }
        // tmp → other
        if (*reinterpret_cast<unsigned char*>(tmpBuf + 15) & 1) {
            _M_copy_chars(other.short_.buf, reinterpret_cast<CharT*>(tmpBuf), 16);
        } else {
            _M_copy_chars(reinterpret_cast<CharT*>(&other.long_),
                          reinterpret_cast<CharT*>(tmpBuf), sizeof(_Long));
        }
    }

private:
    // ---- SSO 存储布局 (32 字节) ----
    struct _Long {
        CharT*  ptr;         // 堆数据指针
        size_t  size;
        size_t  capacity;    // 不含 '\0'
    };

    struct _Short {
        CharT   buf[16];     // SSO 本地缓冲
        // buf[15] 编码：bit0=1 表示短模式，bit1-7 存 (SSO_CAP - size)
    };

    union {
        _Long  long_;
        _Short short_;
    };

    // ---- 辅助函数 ----
    bool _M_is_short() const {
        return (reinterpret_cast<const unsigned char*>(short_.buf)[15] & 1) != 0;
    }

    size_t _M_short_size() const {
        return SSO_CAP - (reinterpret_cast<const unsigned char*>(short_.buf)[15] >> 1);
    }

    void _M_set_short_size(size_t n) {
        short_.buf[n] = '\0';  // ensure null termination
        reinterpret_cast<unsigned char*>(short_.buf)[15] =
            static_cast<unsigned char>(((SSO_CAP - n) << 1) | 1);
    }

    void _M_init_short() {
        short_.buf[0] = '\0';
        _M_set_short_size(0);
    }

    // 当前实际容量（短模式返回 SSO_CAP）
    size_t capacity_impl() const {
        return _M_is_short() ? SSO_CAP : long_.capacity;
    }

    void _M_set_size(size_t n) {
        if (_M_is_short()) {
            _M_set_short_size(n);
        } else {
            long_.size = n;
        }
    }

    CharT* _M_alloc_long(size_t n) {
        return static_cast<CharT*>(::operator new(n * sizeof(CharT)));
    }

    void _M_grow(size_t newCap) {
        // 扩容策略：1.5x 或直接满足需求
        size_t curCap = capacity_impl();
        size_t target = (curCap > 0 && curCap * 3 / 2 > newCap) ? curCap * 3 / 2 : newCap;

        CharT* newPtr = _M_alloc_long(target + 1);
        size_t oldLen = length();
        _M_copy_chars(newPtr, c_str(), oldLen);
        newPtr[oldLen] = '\0';

        // 释放旧内存
        if (!_M_is_short()) {
            ::operator delete(long_.ptr);
        }

        // 切换到长模式
        long_.ptr = newPtr;
        long_.size = oldLen;
        long_.capacity = target;
    }

    static size_t _M_strlen(const CharT* s) {
        size_t n = 0;
        while (*s++) ++n;
        return n;
    }

    static int _M_strcmp(const CharT* a, const CharT* b) {
        while (*a && *a == *b) { ++a; ++b; }
        return static_cast<int>(*a) - static_cast<int>(*b);
    }

    static void _M_copy_chars(CharT* dst, const CharT* src, size_t n) {
        for (size_t i = 0; i < n; ++i) dst[i] = src[i];
    }
};

// 类型别名
using String = basic_string<char>;

// 前向声明 Hash 模板（定义在 LinkedHashMap.h 中）
template<typename K> struct Hash;

// Hash 特化 for String（LinkedHashMap 使用，DJB2 算法）
template<>
struct Hash<String> {
    size_t operator()(const String& key) const {
        size_t hash = 5381;
        const char* s = key.c_str();
        int c;
        while ((c = *s++)) {
            hash = ((hash << 5) + hash) + c;  // hash * 33 + c
        }
        return hash;
    }
};

// 全局 operator+ （支持 "hello " + string + " world"）
inline String operator+(const String& a, const char* b) {
    String tmp(a);
    tmp.append(b);
    return tmp;
}

inline String operator+(const char* a, const String& b) {
    String tmp(a);
    tmp.append(b.c_str(), b.length());
    return tmp;
}

inline String operator+(const String& a, const String& b) {
    String tmp(a);
    tmp.append(b.c_str(), b.length());
    return tmp;
}

} // namespace dns
