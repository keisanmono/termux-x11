#pragma once
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

// Diagnostic-only, called on the existing Activity/UI thread. Owns a separate
// instance of the existing Binder log pipe; never shares the logcat writer fd.
static int r9_sink_fd = -1;
static unsigned long long r9_sink_seq, r9_sink_lost;

static void r9_tracef(const char* format, ...) {
    int saved = errno;
    if (r9_sink_seq >= 8192) { errno = saved; return; }
    ++r9_sink_seq;
    char line[512]; // <= POSIX PIPE_BUF: a nonblocking pipe write is atomic.
    int prefix = snprintf(line, sizeof(line), "R9ArrowTrace seq=%llu lost=%llu pid=%ld ",
                          r9_sink_seq, r9_sink_lost, (long)getpid());
    va_list args;
    va_start(args, format);
    int size = vsnprintf(line + prefix, sizeof(line) - prefix - 1, format, args);
    va_end(args);
    if (r9_sink_fd < 0 || size < 0 || size >= (int)sizeof(line) - prefix - 1) {
        ++r9_sink_lost; errno = saved; return;
    }
    size += prefix;
    line[size++] = '\n';
    // A disconnected diagnostic reader must never terminate the Activity.
    sigset_t block, previous, pending;
    sigemptyset(&block); sigaddset(&block, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &block, &previous) != 0) {
        ++r9_sink_lost; errno = saved; return;
    }
    bool was_pending = sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE);
    ssize_t count = write(r9_sink_fd, line, size); // one try, never waits/retries
    int failure = errno;
    if (count < 0 && failure == EPIPE && !was_pending) {
        struct timespec zero = {};
        sigtimedwait(&block, nullptr, &zero);
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    if (count != size) ++r9_sink_lost;
    if (count < 0 && failure == EPIPE) { close(r9_sink_fd); r9_sink_fd = -1; }
    errno = saved;
}

static bool r9_sink_attach(int fd) {
    int saved = errno;
    if (fd < 0) { errno = saved; return false; }
    if (r9_sink_fd >= 0) { close(fd); errno = saved; return true; }
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
            fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        close(fd); errno = saved; return false;
    }
    r9_sink_fd = fd;
    errno = saved;
    return true;
}
