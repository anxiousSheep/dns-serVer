// Iterator.h — 迭代器标签与 reverse_iterator
#pragma once

#include <cstddef>

namespace dns {

// ==================== 迭代器标签 ====================
struct input_iterator_tag {};
struct output_iterator_tag {};
struct forward_iterator_tag       : public input_iterator_tag {};
struct bidirectional_iterator_tag : public forward_iterator_tag {};
struct random_access_iterator_tag : public bidirectional_iterator_tag {};

// ==================== iterator_traits ====================
template<typename Iterator>
struct iterator_traits {
    using value_type        = typename Iterator::value_type;
    using difference_type   = typename Iterator::difference_type;
    using pointer           = typename Iterator::pointer;
    using reference         = typename Iterator::reference;
    using iterator_category = typename Iterator::iterator_category;
};

// 指针的特化
template<typename T>
struct iterator_traits<T*> {
    using value_type        = T;
    using difference_type   = ptrdiff_t;
    using pointer           = T*;
    using reference         = T&;
    using iterator_category = random_access_iterator_tag;
};

// ==================== reverse_iterator ====================
template<typename Iterator>
class ReverseIterator {
public:
    using value_type        = typename iterator_traits<Iterator>::value_type;
    using difference_type   = typename iterator_traits<Iterator>::difference_type;
    using pointer           = typename iterator_traits<Iterator>::pointer;
    using reference         = typename iterator_traits<Iterator>::reference;
    using iterator_category = typename iterator_traits<Iterator>::iterator_category;

    ReverseIterator() : current() {}
    explicit ReverseIterator(Iterator it) : current(it) {}

    Iterator base() const { return current; }

    reference operator*() const {
        Iterator tmp = current;
        return *(--tmp);
    }

    pointer operator->() const { return &(operator*()); }

    ReverseIterator& operator++() { --current; return *this; }
    ReverseIterator  operator++(int) { auto tmp = *this; --current; return tmp; }
    ReverseIterator& operator--() { ++current; return *this; }
    ReverseIterator  operator--(int) { auto tmp = *this; ++current; return tmp; }

    ReverseIterator operator+(difference_type n) const { return ReverseIterator(current - n); }
    ReverseIterator operator-(difference_type n) const { return ReverseIterator(current + n); }
    ReverseIterator& operator+=(difference_type n) { current -= n; return *this; }
    ReverseIterator& operator-=(difference_type n) { current += n; return *this; }

    reference operator[](difference_type n) const { return *(*this + n); }

    bool operator==(const ReverseIterator& other) const { return current == other.current; }
    bool operator!=(const ReverseIterator& other) const { return current != other.current; }

protected:
    Iterator current;
};

} // namespace dns
