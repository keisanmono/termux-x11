#pragma once
#include "control_io.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <vector>

namespace control {
struct Message {
    std::vector<unsigned char> bytes;
    // Captured resources belong to the message, never to a mutable X pixmap.
    std::function<bool(int)> tail;
    size_t charge = 0;
};

class Writer {
    struct Session {
        int fd;
        std::atomic<bool> failed{false};
        explicit Session(int f) : fd(f) {}
        bool cancel() { if (failed.exchange(true)) return false; shutdown(fd, SHUT_RDWR); return true; }
        ~Session() { close(fd); }
    };
    struct Entry { std::shared_ptr<Session> session; Message message; };
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Entry> queue;
    std::shared_ptr<Session> current;
    pthread_t worker{};
    void (*report)(const char*);
    bool started = false, stopping = false;
    size_t count = 0, bytes = 0;
    void clearLocked() {
        for (auto& e : queue) { --count; bytes -= e.message.charge; }
        queue.clear();
    }
    void run() {
        // Android's opaque AHardwareBuffer sender may use sendmsg without
        // MSG_NOSIGNAL. Confine SIGPIPE blocking to this dedicated thread.
        sigset_t set; sigemptyset(&set); sigaddset(&set, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
        for (;;) {
            Entry e;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || !queue.empty(); });
                if (stopping) return;
                e = std::move(queue.front()); queue.pop_front();
            }
            bool ok = !e.session->failed &&
                control_send_all(e.session->fd, e.message.bytes.data(), e.message.bytes.size()) == 0 &&
                (!e.message.tail || e.message.tail(e.session->fd));
            if (!ok && e.session->cancel() && report) report("send failed or timed out");
            {
                std::lock_guard<std::mutex> lock(mutex);
                --count; bytes -= e.message.charge;
                if (e.session == current && e.session->failed) clearLocked();
            }
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
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            if (current) current->cancel();
            clearLocked();
        }
        changed.notify_one();
        if (started) pthread_join(worker, nullptr);
    }
    bool connect(int fd) {
        int copy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (copy < 0) return false;
        // Bound opaque Android handle writes too. No socket O_NONBLOCK change.
        struct timeval timeout = {0, 250000};
        if (setsockopt(copy, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) {
            close(copy); return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (current) current->cancel();
        clearLocked();
        current = std::make_shared<Session>(copy);
        return true;
    }
    void disconnect() {
        std::lock_guard<std::mutex> lock(mutex);
        if (current) current->cancel();
        current.reset(); clearLocked();
    }
    void fail() {
        std::lock_guard<std::mutex> lock(mutex);
        if (current && current->cancel() && report) report("message resource unavailable or too large");
        clearLocked();
    }
    bool enqueue(Message message) {
        message.charge += message.bytes.size();
        std::lock_guard<std::mutex> lock(mutex);
        if (!started || !current || current->failed) return false;
        if (count >= maxMessages || message.charge > maxBytes || bytes > maxBytes - message.charge) {
            if (current->cancel() && report) report("queue limit exceeded");
            clearLocked(); return false;
        }
        ++count; bytes += message.charge;
        queue.push_back({current, std::move(message)});
        changed.notify_one();
        return true;
    }
};
}
