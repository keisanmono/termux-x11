#pragma once
#include "control_io.h"
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

// Runtime-free C++: Xlorie intentionally links with -nostdlib++.
namespace control {
struct Message {
    unsigned char* bytes;
    size_t size, charge;
    void* resource;
    bool (*sendTail)(int, void*);
    void (*release)(void*);
};

inline Message* createMessage(size_t size) {
    auto* m = static_cast<Message*>(calloc(1, sizeof(Message)));
    if (!m) return nullptr;
    if (size && !(m->bytes = static_cast<unsigned char*>(malloc(size)))) {
        free(m); return nullptr;
    }
    m->size = size;
    return m;
}
inline void destroyMessage(Message* m) {
    if (!m) return;
    if (m->release) m->release(m->resource);
    free(m->bytes);
    free(m);
}

class Writer {
    struct Session {
        int fd;
        unsigned refs; // Protected by mutex, including the in-flight entry.
        bool failed;   // Also read by the worker outside mutex.
    };
    struct Entry { Session* session; Message* message; };
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
    Entry queue[256]{};
    size_t head = 0, queued = 0, count = 0, bytes = 0;
    Session* current = nullptr;
    pthread_t worker{};
    void (*report)(const char*);
    bool started = false, stopping = false;

    static bool cancel(Session* session) {
        if (!session || __atomic_exchange_n(&session->failed, true, __ATOMIC_ACQ_REL)) return false;
        shutdown(session->fd, SHUT_RDWR);
        return true;
    }
    static void releaseSession(Session* session) {
        if (session && --session->refs == 0) {
            close(session->fd); free(session);
        }
    }
    void clearLocked() {
        while (queued) {
            Entry& e = queue[head];
            --count; bytes -= e.message->charge;
            destroyMessage(e.message); releaseSession(e.session);
            e = {}; head = (head + 1) % maxMessages; --queued;
        }
    }
    void run() {
        // Android's opaque handle sender may omit MSG_NOSIGNAL. Confine SIGPIPE
        // blocking to this dedicated thread; never change process-wide handlers.
        sigset_t set; sigemptyset(&set); sigaddset(&set, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
        for (;;) {
            pthread_mutex_lock(&mutex);
            while (!stopping && !queued) pthread_cond_wait(&changed, &mutex);
            if (stopping) { pthread_mutex_unlock(&mutex); return; }
            Entry e = queue[head]; queue[head] = {};
            head = (head + 1) % maxMessages; --queued;
            pthread_mutex_unlock(&mutex);

            bool ok = !__atomic_load_n(&e.session->failed, __ATOMIC_ACQUIRE) &&
                control_send_all(e.session->fd, e.message->bytes, e.message->size) == 0 &&
                (!e.message->sendTail || e.message->sendTail(e.session->fd, e.message->resource));
            if (!ok && cancel(e.session) && report) report("send failed or timed out");

            size_t charge = e.message->charge;
            destroyMessage(e.message);
            pthread_mutex_lock(&mutex);
            --count; bytes -= charge;
            if (e.session == current && __atomic_load_n(&e.session->failed, __ATOMIC_ACQUIRE)) clearLocked();
            releaseSession(e.session);
            pthread_mutex_unlock(&mutex);
        }
    }
public:
    static constexpr size_t maxMessages = 256, maxBytes = 4 * 1024 * 1024;
    explicit Writer(void (*failure)(const char*) = nullptr) : report(failure) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    bool start() {
        if (started) return true;
        started = pthread_create(&worker, nullptr, [](void* p) -> void* {
            static_cast<Writer*>(p)->run(); return nullptr;
        }, this) == 0;
        return started;
    }
    ~Writer() {
        pthread_mutex_lock(&mutex);
        stopping = true; cancel(current); clearLocked();
        pthread_cond_signal(&changed);
        pthread_mutex_unlock(&mutex);
        if (started) pthread_join(worker, nullptr);
        releaseSession(current);
        pthread_cond_destroy(&changed); pthread_mutex_destroy(&mutex);
    }
    bool connect(int fd) {
        int copy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (copy < 0) return false;
        // Bound opaque Android handle writes too, without setting O_NONBLOCK.
        struct timeval timeout = {0, 250000};
        auto* next = static_cast<Session*>(calloc(1, sizeof(Session)));
        if (!next || setsockopt(copy, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) {
            close(copy); free(next); return false;
        }
        next->fd = copy; next->refs = 1;
        pthread_mutex_lock(&mutex);
        cancel(current); clearLocked(); releaseSession(current); current = next;
        pthread_mutex_unlock(&mutex);
        return true;
    }
    void disconnect() {
        pthread_mutex_lock(&mutex);
        cancel(current); clearLocked(); releaseSession(current); current = nullptr;
        pthread_mutex_unlock(&mutex);
    }
    void fail() {
        pthread_mutex_lock(&mutex);
        if (cancel(current) && report) report("message resource unavailable or too large");
        clearLocked();
        pthread_mutex_unlock(&mutex);
    }
    // Always takes ownership, including rejection and allocation failure.
    bool enqueue(Message* message) {
        if (!message) { fail(); return false; }
        pthread_mutex_lock(&mutex);
        bool accepted = false;
        if (started && current && !__atomic_load_n(&current->failed, __ATOMIC_ACQUIRE)) {
            if (message->size > maxBytes || message->charge > maxBytes - message->size ||
                count >= maxMessages || bytes > maxBytes - message->size - message->charge) {
                if (cancel(current) && report) report("queue limit exceeded");
                clearLocked();
            } else {
                message->charge += message->size;
                ++count; bytes += message->charge; ++current->refs;
                queue[(head + queued) % maxMessages] = {current, message}; ++queued;
                pthread_cond_signal(&changed);
                accepted = true;
            }
        }
        pthread_mutex_unlock(&mutex);
        if (!accepted) destroyMessage(message);
        return accepted;
    }
};
}
