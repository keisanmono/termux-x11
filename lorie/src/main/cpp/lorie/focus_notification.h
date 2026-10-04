#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <unistd.h>

// A focus change invalidates IME composition state; it carries no payload.
// A separate eventfd coalesces changes while the UI is not consuming them.
// Never put these notifications in the control stream: a stalled UI must not
// block the X server or split a clipboard/buffer/SCM_RIGHTS message.
namespace focus_notification {

inline int create() {
    return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

inline int duplicate(int fd) {
    return fcntl(fd, F_DUPFD_CLOEXEC, 0);
}

inline bool notify(int fd) {
    const uint64_t one = 1;
    ssize_t result;
    do {
        result = write(fd, &one, sizeof(one));
    } while (result < 0 && errno == EINTR);
    // A saturated eventfd is already readable: the invalidation is pending.
    return result == static_cast<ssize_t>(sizeof(one)) || (result < 0 && errno == EAGAIN);
}

enum class ReadResult { changed, empty, error };

inline ReadResult consume(int fd) {
    uint64_t count;
    ssize_t result;
    do {
        result = read(fd, &count, sizeof(count));
    } while (result < 0 && errno == EINTR);
    if (result == static_cast<ssize_t>(sizeof(count)) && count != 0)
        return ReadResult::changed;
    if (result < 0 && errno == EAGAIN)
        return ReadResult::empty;
    return ReadResult::error;
}

} // namespace focus_notification
