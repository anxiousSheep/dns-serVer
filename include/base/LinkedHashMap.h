// LinkedHashMap.h — Java 风格 LRU 哈希链表
// 每个节点同时参与哈希桶链表（h_next）和全局双向链表（prev/next）
// 复现 Java LinkedHashMap 设计，不做 HashMap+List 两份元数据
#pragma once

#include "Allocator.h"
#include "Utility.h"
#include <cstddef>

namespace dns {

// ==================== 默认哈希函数 ====================
template<typename K>
struct Hash {
    size_t operator()(const K& key) const;
};

// 字符串特化：DJB2 算法（与 C 版一致）
template<>
struct Hash<const char*> {
    size_t operator()(const char* s) const {
        size_t hash = 5381;
        int c;
        while ((c = *s++)) {
            hash = ((hash << 5) + hash) + c;  // hash * 33 + c
        }
        return hash;
    }
};

// 整数特化
template<>
struct Hash<int> {
    size_t operator()(int key) const { return static_cast<size_t>(key); }
};

template<>
struct Hash<unsigned int> {
    size_t operator()(unsigned int key) const { return static_cast<size_t>(key); }
};

template<>
struct Hash<unsigned short> {
    size_t operator()(unsigned short key) const { return static_cast<size_t>(key); }
};

// ==================== LinkedHashMap ====================
template<typename K, typename V>
class LinkedHashMap {
public:
    using SizeType = size_t;

    // accessOrder:
    //   true  → LRU模式：get() 后移到头部（用于 DNS 缓存，热域名常驻）
    //   false → 插入顺序：遍历时保持插入顺序（用于加载 dnsrelay.txt）
    explicit LinkedHashMap(SizeType capacity, bool accessOrder = true)
        : capacity_(capacity), size_(0), accessOrder(accessOrder) {
        // 桶数为接近 2*capacity 的质数，降低冲突
        bucketCount = _M_nextPrime(capacity * 2 + 1);
        buckets = new Node*[bucketCount];
        for (SizeType i = 0; i < bucketCount; ++i)
            buckets[i] = nullptr;

        // 哨兵节点
        head = new Node(K(), V());
        tail = new Node(K(), V());
        head->next = tail;
        tail->prev = head;
    }

    ~LinkedHashMap() {
        // 释放所有数据节点
        Node* cur = head->next;
        while (cur != tail) {
            Node* next = cur->next;
            delete cur;
            cur = next;
        }
        delete head;
        delete tail;
        delete[] buckets;
    }

    LinkedHashMap(const LinkedHashMap&) = delete;
    LinkedHashMap& operator=(const LinkedHashMap&) = delete;

    // ---- 查找 ----
    // 返回 value 指针（命中）或 nullptr（未命中）
    // LRU 模式下命中后节点移到链表头部
    V* get(const K& key) {
        SizeType idx = _M_bucketIndex(key);
        Node* node = _M_findInBucket(idx, key);

        if (node) {
            if (accessOrder)
                _M_moveToHead(node);
            return &node->value;
        }
        return nullptr;
    }

    // ---- 插入/更新 ----
    // key 存在则更新 value 并移到头部；不存在则插入，超出容量时自动淘汰尾部
    void put(const K& key, const V& value) {
        SizeType idx = _M_bucketIndex(key);
        Node* node = _M_findInBucket(idx, key);

        if (node) {
            // key 已存在：更新 value，移到 MRU 端
            node->value = value;
            if (accessOrder)
                _M_moveToHead(node);
        } else {
            // key 不存在：新建节点
            Node* newNode = new Node(key, value);

            // 插入桶链表（头插）
            _M_insertToBucket(idx, newNode);

            // 插入 LRU 链表头部
            _M_linkToHead(newNode);
            ++size_;

            // 容量检查 → 淘汰尾部
            if (size_ > capacity_)
                _M_evictLRU();
        }
    }

    // ---- 删除 ----
    bool remove(const K& key) {
        SizeType idx = _M_bucketIndex(key);
        Node* node = _M_findInBucket(idx, key);
        if (!node) return false;

        _M_unlink(node);
        _M_removeFromBucket(idx, node);
        delete node;
        --size_;
        return true;
    }

    // ---- 查询 ----
    bool contains(const K& key) const {
        SizeType idx = _M_bucketIndex(key);
        return const_cast<LinkedHashMap*>(this)->_M_findInBucket(idx, key) != nullptr;
    }

    SizeType size()     const { return size_; }
    SizeType capacity() const { return capacity_; }
    bool     empty()    const { return size_ == 0; }

    // ---- 遍历（按LRU顺序，从最新到最旧）----
    // 回调函数签名: void(const K& key, const V& value)
    template<typename Callback>
    void forEach(Callback cb) const {
        for (Node* cur = head->next; cur != tail; cur = cur->next) {
            cb(cur->key, cur->value);
        }
    }

    // 逆序遍历（从最旧到最新）
    template<typename Callback>
    void forEachReverse(Callback cb) const {
        for (Node* cur = tail->prev; cur != head; cur = cur->prev) {
            cb(cur->key, cur->value);
        }
    }

    // 获取 LRU 链表头部（最新）的值
    V* newest() {
        Node* n = head->next;
        return (n != tail) ? &n->value : nullptr;
    }

    // 获取 LRU 链表尾部（最旧）的值
    V* oldest() {
        Node* n = tail->prev;
        return (n != head) ? &n->value : nullptr;
    }

private:
    // ===== 节点结构（双角色） =====
    struct Node {
        K     key;
        V     value;
        Node* prev;    // LRU 链表前驱
        Node* next;    // LRU 链表后继
        Node* h_next;  // 哈希桶链表后继

        Node(const K& k, const V& v)
            : key(k), value(v), prev(nullptr), next(nullptr), h_next(nullptr) {}
    };

    // ===== 数据成员 =====
    Node**    buckets;
    SizeType  bucketCount;
    SizeType  capacity_;
    SizeType  size_;
    bool      accessOrder;

    Node*     head;   // MRU 哨兵（最新访问端）
    Node*     tail;   // LRU 哨兵（淘汰端）

    // ===== 哈希表操作 =====
    SizeType _M_bucketIndex(const K& key) const {
        return Hash<K>()(key) % bucketCount;
    }

    Node* _M_findInBucket(SizeType idx, const K& key) {
        for (Node* cur = buckets[idx]; cur; cur = cur->h_next) {
            if (cur->key == key)
                return cur;
        }
        return nullptr;
    }

    void _M_insertToBucket(SizeType idx, Node* node) {
        node->h_next = buckets[idx];
        buckets[idx] = node;
    }

    void _M_removeFromBucket(SizeType idx, Node* target) {
        Node** cur = &buckets[idx];
        while (*cur) {
            if (*cur == target) {
                *cur = target->h_next;
                return;
            }
            cur = &(*cur)->h_next;
        }
    }

    // ===== LRU 双向链表操作 =====
    void _M_linkToHead(Node* node) {
        node->next = head->next;
        node->prev = head;
        head->next->prev = node;
        head->next = node;
    }

    void _M_unlink(Node* node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    void _M_moveToHead(Node* node) {
        _M_unlink(node);
        _M_linkToHead(node);
    }

    // ===== LRU 淘汰 =====
    void _M_evictLRU() {
        Node* victim = tail->prev;
        if (victim == head) return;  // 链表为空（理论上不会）

        SizeType idx = _M_bucketIndex(victim->key);
        _M_unlink(victim);
        _M_removeFromBucket(idx, victim);
        delete victim;
        --size_;
    }

    // ===== 质数生成 =====
    static bool _M_isPrime(SizeType n) {
        if (n <= 1) return false;
        if (n <= 3) return true;
        if (n % 2 == 0 || n % 3 == 0) return false;
        for (SizeType i = 5; i * i <= n; i += 6)
            if (n % i == 0 || n % (i + 2) == 0) return false;
        return true;
    }

    static SizeType _M_nextPrime(SizeType n) {
        while (!_M_isPrime(n)) ++n;
        return n;
    }
};

} // namespace dns
