#pragma once

#include <poll.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <chrono>
#include <queue>
#include <set>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <atomic>

class Poller {
public:
    using FdCallback = std::function<void(int fd, short events, short revents)>;

    Poller();
    ~Poller();

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&&) = delete;
    Poller& operator=(Poller&&) = delete;

    bool add(int fd, short events, FdCallback callback);

    bool remove(int fd);

    bool update_events(int fd, short events);

    int poll(int timeout_ms = -1);

    void run(int default_timeout_ms = 1000);

    void stop();

    void start();

    bool is_running() const;

    size_t get_fd_count() const;

    void trigger_loop() const;

private:
    struct FdInfo {
        FdCallback callback;
        short events;
        bool active;
        FdInfo(FdCallback cb, short ev) : callback(std::move(cb)), events(ev), active(true) {}
    };

    mutable std::shared_mutex mtx;
    int pipe_fds[2];
    std::unordered_map<int, std::unique_ptr<FdInfo>> fd_map_;
    std::atomic<bool> running_{false};
    bool processing_loop_{false};

    void close_pipe();

    void handle_pipe_callback(int fd, short events, short revents);

    bool create_pipe();

};

