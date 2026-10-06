#pragma once
#include <stdint.h>
#include <time.h>

// Diagnostic-only branch. One bounded window per endpoint, no protocol fields.
// These hooks run on the existing Android UI / X server input callback threads.
struct R9ArrowTraceWindow {
    uint64_t start_ns;
    unsigned lines;
};

static inline bool r9_arrow_allow(struct R9ArrowTraceWindow* window, int key, uint64_t now) {
    if (key != 111 && key != 113 && key != 114 && key != 116) return false;
    if (!window->start_ns) window->start_ns = now;
    if (now < window->start_ns || now - window->start_ns >= UINT64_C(10000000000)
            || window->lines >= 2048) return false;
    ++window->lines;
    return true;
}

static inline uint64_t r9_arrow_stamp(int key) {
    if (key != 111 && key != 113 && key != 114 && key != 116) return 0;
    static struct R9ArrowTraceWindow window = {};
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    uint64_t now = (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
    return r9_arrow_allow(&window, key, now) ? now : 0;
}
