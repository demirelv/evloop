#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include "evloop.h"
//#define DEBUG

#ifdef DEBUG
#define dbg(a...) do { \
    std::cerr << "[DEBUG] " << __FILE__ << ":" << __LINE__ << ":" << __FUNCTION__ <<" "; \
    fprintf(stderr, a); \
    std::cerr << std::endl; \
} while(0)
#else
#define dbg(fmt, ...) do { } while(0)
#endif

Timer::Timer(): next_timer_id_(1) {
}

size_t Timer::get_timer_count() const {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return timer_map_.size();
}

void Timer::timer_add_queue(std::shared_ptr<TimerInfo> timer) {
    auto it = timer_queue_.begin();
    while (it != timer_queue_.end() && (*it)->next_fire <= timer->next_fire) {
        ++it;
    }
    timer->updated = false;
    timer_queue_.insert(it, timer);
}

int Timer::find_id() {
        int id = next_timer_id_;
        while (timer_map_.find(id) != timer_map_.end()) {
            id++;
            if (id <= 0) {
                return -1;
            }
        }
        next_timer_id_ = id + 1;
        return id;
    }


int Timer::add_timer(int interval_ms, TimerCallback callback, bool repeat) { 
    std::unique_lock<std::shared_mutex> lock(mtx);
    if (interval_ms <= 0 || !callback) {
        return -1;
    }

    int timer_id = find_id();
    if (timer_id <= 0)
        return -1;

    auto now = std::chrono::steady_clock::now();
    auto interval = std::chrono::milliseconds(interval_ms);
    auto next_fire = now + interval;

    auto timer_info = std::make_shared<TimerInfo>(
        timer_id, std::move(callback), next_fire, interval, repeat);

    timer_map_[timer_id] = timer_info;
    timer_add_queue(timer_info);
    return timer_id;
}

bool Timer::update_timer_interval(int timer_id, int interval_ms) {
    std::unique_lock<std::shared_mutex> lock(mtx);
    auto it = timer_map_.find(timer_id);
    if (it == timer_map_.end() || interval_ms <= 0) {
        return false;
    }
    timer_queue_.remove(it->second);

    auto now = std::chrono::steady_clock::now();
    auto new_interval = std::chrono::milliseconds(interval_ms);

    it->second->interval = new_interval;
    it->second->next_fire = now + new_interval;
    it->second->updated = true;
    return true;
}

void Timer::process_timers() {
    auto now = std::chrono::steady_clock::now();
    std::unique_lock<std::shared_mutex> lock(mtx);

    while (!timer_queue_.empty()) {
        auto timer_info = timer_queue_.front();

        if (!timer_info->active) {
            dbg("timer not active %d", timer_info->active);
            timer_queue_.pop_front();
            continue;
        }
        auto remaining = timer_info->next_fire - now;
        if (remaining > std::chrono::microseconds(500)) {
            break;
        }

        timer_queue_.pop_front();
        lock.unlock();
        try {
            timer_info->callback(timer_info->id);
       } catch (const std::exception& e) {
            dbg("Exception in timer callback for timer %d %s", timer_info->id, e.what().c_str());
            timer_info->active = false;
        } catch (...) {
            dbg("Unknown exception in timer callback for timer %d", timer_info->id);
            timer_info->active = false;
        }
        lock.lock();
        if (timer_info->repeat && timer_info->active) {
            timer_info->next_fire = now + timer_info->interval;
            timer_add_queue(timer_info);
        } else {
            timer_info->active = false;
        }
    }

    for (auto it = timer_map_.begin(); it != timer_map_.end();) {
        if (!it->second->active) {
            next_timer_id_ = it->second->id;
            it = timer_map_.erase(it);
        } else {
            if (it->second->updated)
                timer_add_queue(it->second);
            ++it;
        }
    }
}

int Timer::calculate_timeout(int default_timeout_ms) const { 
    std::unique_lock<std::shared_mutex> lock(mtx);
    if (timer_queue_.empty()) {
        return default_timeout_ms;
    }

    auto now = std::chrono::steady_clock::now();
    auto next_timer = *timer_queue_.begin();

    if (!next_timer->active) {
        return 0;
    }

    auto time_to_next = std::chrono::duration_cast<std::chrono::milliseconds>(
        next_timer->next_fire - now + std::chrono::microseconds(500));

    if (time_to_next.count() <= 0) {
        return 0;
    }

    int timeout = static_cast<int>(time_to_next.count());

    if (default_timeout_ms < 0) {
        return timeout;
    }
    return std::min(timeout, default_timeout_ms);
}

bool Timer::remove_timer(int timer_id) {
    std::unique_lock<std::shared_mutex> lock(mtx);
    auto it = timer_map_.find(timer_id);
    if (it == timer_map_.end()) {
        return false;
    }
    dbg("timer deactived %d", timer_id);
    it->second->active = false;
    return true;
}

