// Epoll.h — Cross-platform event notification RAII wrapper
// Linux: uses epoll; Windows: uses select()
// Replaces blocking recvfrom with event-driven model
#pragma once

#include "../base/Vector.h"
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#else
#include <sys/epoll.h>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace dns {

// Cross-platform event type constants (same values as Linux epoll for
// convenience)
#ifndef EPOLLIN
#define EPOLLIN  0x001
#endif
#ifndef EPOLLOUT
#define EPOLLOUT 0x004
#endif
#ifndef EPOLLERR
#define EPOLLERR 0x008
#endif

// Cross-platform event struct
struct PollEvent {
    int      fd;
    uint32_t events;  // bitmask of EPOLLIN/EPOLLOUT/EPOLLERR
};

class Epoller {
public:
    static const int MAX_EVENTS = 64;

    Epoller() {
#ifdef _WIN32
        maxFd = -1;
        FD_ZERO(&readSet);
#else
        epfd = epoll_create1(EPOLL_CLOEXEC);
#endif
    }

    ~Epoller() {
#ifndef _WIN32
        if (epfd >= 0) close(epfd);
#endif
    }

    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;

    Epoller(Epoller&& other) noexcept {
#ifdef _WIN32
        maxFd = other.maxFd;
        readSet = other.readSet;
        other.maxFd = -1;
        FD_ZERO(&other.readSet);
#else
        epfd = other.epfd;
        other.epfd = -1;
#endif
    }

    // Register fd for events (EPOLLIN only supported on Windows)
    void add(int fd, uint32_t events) {
#ifdef _WIN32
        fds.push_back(FdEntry{fd, events});
        if (events & EPOLLIN) {
            FD_SET(fd, &readSet);
        }
        if (fd > maxFd) maxFd = fd;
#else
        struct epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
#endif
    }

    // Modify fd events
    void mod(int fd, uint32_t events) {
#ifdef _WIN32
        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].fd == fd) {
                fds[i].events = events;
                FD_CLR(fd, &readSet);
                if (events & EPOLLIN) FD_SET(fd, &readSet);
                return;
            }
        }
#else
        struct epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
#endif
    }

    // Remove fd
    void del(int fd) {
#ifdef _WIN32
        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].fd == fd) {
                fds[i].fd = -1;  // mark as unused
                FD_CLR(fd, &readSet);
                // Recalc maxFd
                maxFd = -1;
                for (size_t j = 0; j < fds.size(); ++j) {
                    if (fds[j].fd > maxFd) maxFd = fds[j].fd;
                }
                return;
            }
        }
#else
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
#endif
    }

    // Wait for events
    // timeoutMs: -1=block forever, 0=return immediately, >0=timeout
    // milliseconds Returns list of ready events; empty on timeout
    Vector<PollEvent> wait(int timeoutMs) {
        Vector<PollEvent> result;
#ifdef _WIN32
        fd_set readCopy = readSet;
        struct timeval tv, *ptv = nullptr;
        if (timeoutMs >= 0) {
            tv.tv_sec  = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            ptv = &tv;
        }

        int n = select(maxFd + 1, &readCopy, nullptr, nullptr, ptv);
        if (n <= 0) return result;

        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].fd >= 0 && FD_ISSET(fds[i].fd, &readCopy)) {
                PollEvent ev;
                ev.fd     = fds[i].fd;
                ev.events = EPOLLIN;
                result.push_back(ev);
            }
        }
#else
        struct epoll_event evs[MAX_EVENTS];
        int n = epoll_wait(epfd, evs, MAX_EVENTS, timeoutMs);
        for (int i = 0; i < n; ++i) {
            PollEvent ev;
            ev.fd     = evs[i].data.fd;
            ev.events = evs[i].events;
            result.push_back(ev);
        }
#endif
        return result;
    }

    int fd() const {
#ifdef _WIN32
        return -1;  // select has no single fd
#else
        return epfd;
#endif
    }

private:
#ifdef _WIN32
    struct FdEntry {
        int      fd;
        uint32_t events;
    };
    Vector<FdEntry> fds;
    fd_set          readSet;
    int             maxFd;
#else
    int epfd;
#endif
};

} // namespace dns
