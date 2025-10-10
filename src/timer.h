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

class Timer {
public:
    using TimerCallback = std::function<void(int timer_id)>;
    using TimePoint = std::chrono::steady_clock::time_point;

    Timer();
    ~Timer() {};
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    int add_timer(int interval_ms, TimerCallback callback, bool repeat = true);
    bool remove_timer(int timer_id);
    bool update_timer_interval(int timer_id, int interval_ms);
    size_t get_timer_count() const;
private:
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
    mutable std::shared_mutex mtx;
    std::unordered_map<int, std::shared_ptr<TimerInfo>> timer_map_;
    std::list<std::shared_ptr<TimerInfo>> timer_queue_;

    int next_timer_id_;
    void timer_add_queue(std::shared_ptr<TimerInfo> timer);
    int find_id();
    void timer_qinfo();
protected:
    void process_timers();
    int calculate_timeout(int default_timeout_ms) const;
};

