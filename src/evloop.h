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

class EvLoop {
public:
    using FdCallback = std::function<void(int fd, short events, short revents)>;
    using TimerCallback = std::function<void(int timer_id)>;
    using TimePoint = std::chrono::steady_clock::time_point;

    EvLoop();
    ~EvLoop();

    EvLoop(const EvLoop&) = delete;
    EvLoop& operator=(const EvLoop&) = delete;

    EvLoop(EvLoop&&) = default;
    EvLoop& operator=(EvLoop&&) = default;

    bool add_fd(int fd, short events, FdCallback callback);

    bool remove_fd(int fd);

    bool update_events(int fd, short events);

    int add_timer(int interval_ms, TimerCallback callback, bool repeat = true);

    bool remove_timer(int timer_id);

    bool update_timer_interval(int timer_id, int interval_ms);

    int poll(int timeout_ms = -1);

    void run(int default_timeout_ms = 1000);

    void stop();

    size_t get_fd_count() const;

    size_t get_timer_count() const;

    bool is_watching(int fd) const;

private:
    struct FdInfo {
        FdCallback callback;
        short events;
        bool active;
        FdInfo(FdCallback cb, short ev) : callback(std::move(cb)), events(ev), active(true) {}
    };

    struct TimerInfo {
        int id;
        TimerCallback callback;
        TimePoint next_fire;
        std::chrono::milliseconds interval;
        bool repeat;
        bool active;
        bool updated;
        TimerInfo(int timer_id, TimerCallback cb, TimePoint fire_time,
                  std::chrono::milliseconds intv, bool rep)
            : id(timer_id), callback(std::move(cb)), next_fire(fire_time),
            interval(intv), repeat(rep), active(true), updated(false) {}
       
    };

    int pipe_fds[2];
    std::unordered_map<int, std::unique_ptr<FdInfo>> fd_map_;
    std::unordered_map<int, std::shared_ptr<TimerInfo>> timer_map_;
    std::list<std::shared_ptr<TimerInfo>> timer_queue_;

    std::vector<pollfd> poll_fds_;
    bool running_;
    int next_timer_id_;

    void process_timers();

    bool processing_loop_;

    void rebuildPollArray();

    int calculate_timeout(int default_timeout_ms) const;

    void close_pipe();

    void handle_pipe_callback(int fd, short events, short revents);

    bool create_pipe();

    void trigger_loop();
    void timer_add_queue(std::shared_ptr<TimerInfo> timer);
    void timer_qinfo();
};

