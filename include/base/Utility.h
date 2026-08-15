// Utility.h — move/forward/swap/pair 的手写实现
#pragma once

#include <cstddef>

namespace dns {

// ==================== remove_reference ====================
template<typename T> struct remove_reference      { using type = T; };
template<typename T> struct remove_reference<T&>  { using type = T; };
template<typename T> struct remove_reference<T&&> { using type = T; };
template<typename T> using remove_reference_t = typename remove_reference<T>::type;

// ==================== move ====================
template<typename T>
remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<remove_reference_t<T>&&>(t);
}

// ==================== forward ====================
template<typename T>
T&& forward(remove_reference_t<T>& t) noexcept {
    return static_cast<T&&>(t);
}

template<typename T>
T&& forward(remove_reference_t<T>&& t) noexcept {
    return static_cast<T&&>(t);
}

// ==================== swap ====================
template<typename T>
void swap(T& a, T& b) noexcept {
    T tmp = move(a);
    a = move(b);
    b = move(tmp);
}

// ==================== pair ====================
template<typename T1, typename T2>
struct Pair {
    T1 first;
    T2 second;

    Pair() : first(), second() {}
    Pair(const T1& f, const T2& s) : first(f), second(s) {}
    Pair(T1&& f, T2&& s) : first(move(f)), second(move(s)) {}

    template<typename U1, typename U2>
    Pair(U1&& f, U2&& s) : first(forward<U1>(f)), second(forward<U2>(s)) {}

    Pair(const Pair&) = default;
    Pair(Pair&& other) noexcept
        : first(move(other.first)), second(move(other.second)) {}

    Pair& operator=(Pair other) {
        swap(first, other.first);
        swap(second, other.second);
        return *this;
    }
};

// ==================== true_type / false_type ====================
struct true_type  { static constexpr bool value = true;  };
struct false_type { static constexpr bool value = false; };

// ==================== enable_if ====================
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> { using type = T; };

template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

// ==================== is_integral (简化) ====================
template<typename T> struct is_integral                     : false_type {};
template<> struct is_integral<bool>                         : true_type {};
template<> struct is_integral<char>                         : true_type {};
template<> struct is_integral<signed char>                  : true_type {};
template<> struct is_integral<unsigned char>                : true_type {};
template<> struct is_integral<short>                        : true_type {};
template<> struct is_integral<unsigned short>               : true_type {};
template<> struct is_integral<int>                          : true_type {};
template<> struct is_integral<unsigned int>                 : true_type {};
template<> struct is_integral<long>                         : true_type {};
template<> struct is_integral<unsigned long>                : true_type {};
template<> struct is_integral<long long>                    : true_type {};
template<> struct is_integral<unsigned long long>           : true_type {};

} // namespace dns
