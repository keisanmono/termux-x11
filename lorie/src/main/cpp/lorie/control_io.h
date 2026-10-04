#pragma once
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// One complete byte segment, with a total deadline. Never change socket flags:
// the other direction and Android's handle-transfer API still use this socket.
static inline int control_send_all(int fd, const void* data, size_t size) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t deadline = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000 + 250;
    const char* p = (const char*) data;
    while (size) {
        ssize_t n = send(fd, p, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) { p += n; size -= n; }
        else if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK))
            return -1;
        if (!size) return 0;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        int64_t left = deadline - (ts.tv_sec * 1000LL + ts.tv_nsec / 1000000);
        if (left <= 0) { errno = ETIMEDOUT; return -1; }
        struct pollfd pfd = {fd, POLLOUT, 0};
        int r = poll(&pfd, 1, (int) left);
        if (r == 0) { errno = ETIMEDOUT; return -1; }
        if (r < 0 && errno != EINTR) return -1;
        if (r > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return -1;
    }
    return 0;
}

static inline int control_recv_all(int fd, void* data, size_t size) {
    char* p = (char*) data;
    while (size) {
        ssize_t n = recv(fd, p, size, 0);
        if (n > 0) { p += n; size -= n; }
        else if (n == 0 || errno != EINTR) return -1;
    }
    return 0;
}
