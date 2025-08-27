#include "evloop.h"
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#define DEBUG

#ifdef DEBUG
#define dbg(a...) do { \
    std::cerr << "[DEBUG] " << __FILE__ << ":" << __LINE__ << ":" << __FUNCTION__ <<" "; \
    fprintf(stderr, a); \
    std::cerr << std::endl; \
} while(0)
#else
#define dbg(fmt, ...) do { } while(0)
#endif

void EvLoop::close_pipe() {
    if (pipe_fds[0] > 0) {
        remove_fd(pipe_fds[0]);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        pipe_fds[0] = pipe_fds[1] = -1;
    }
}

EvLoop::EvLoop() : running_(false), next_timer_id_(1), processing_loop_(false) {
    poll_fds_.reserve(64);
    pipe_fds[0] = pipe_fds[1] = -1;
    create_pipe();
}

EvLoop::~EvLoop() {
    stop();
    close_pipe();
}

bool EvLoop::add_fd(int fd, short events, FdCallback callback) {
    if (fd < 0 || !callback) {
        return false;
    }

    if (fd_map_.find(fd) != fd_map_.end()) {
        std::cerr << "Warning: FD " << fd << " is already being watched" << std::endl;
        return false;
    }
    fd_map_[fd] = std::make_unique<FdInfo>(std::move(callback), events);
    dbg("fd added %d", fd);
    return true;
}

bool EvLoop::remove_fd(int fd) {
    auto it = fd_map_.find(fd);
    if (it == fd_map_.end()) {
        return false;
    }
    it->second->active = false;
    dbg("fd deactivated %d", fd);
    return true;
}

bool EvLoop::update_events(int fd, short events) {
    auto it = fd_map_.find(fd);
    if (it == fd_map_.end()) {
        return false;
    }
    it->second->events = events;
    dbg("updated events fd %d, %04x", fd, events);
    return true;
}

bool EvLoop::create_pipe() {

    if (pipe(pipe_fds) != 0) {
        std::cerr << "Failed to create pipe: " << strerror(errno) << std::endl;
        return false;
    }

    int flags = fcntl(pipe_fds[0], F_GETFL);
    if (flags == -1 || fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set pipe non-blocking: " << strerror(errno) << std::endl;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        pipe_fds[0] = pipe_fds[1] = -1;
        return false;
    }

    flags = fcntl(pipe_fds[1], F_GETFL);
    if (flags == -1 || fcntl(pipe_fds[1], F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set pipe write end non-blocking: " << strerror(errno) << std::endl;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        pipe_fds[0] = pipe_fds[1] = -1;
        return false;
    }

    auto callback = [this](int fd, short events, short revents) {
        handle_pipe_callback(fd, events, revents);
    };

    if (!add_fd(pipe_fds[0], POLLIN, callback)) {
        std::cerr << "Failed to add pipe to event loop" << std::endl;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        pipe_fds[0] = pipe_fds[1] = -1;
        return false;
    }

    return true;
}

void EvLoop::handle_pipe_callback(int fd, short events, short revents) {

    (void)events;

    if (revents & POLLIN) {
        char buffer[256];
        ssize_t bytes_read;
        do {
            bytes_read = read(fd, buffer, sizeof(buffer));
        } while (bytes_read > 0);
    }

    if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
    }
}

void EvLoop::trigger_loop() {
    if (pipe_fds[1] > -1) {
        char wake_byte = 1;
        ssize_t result = write(pipe_fds[1], &wake_byte, 1);

        if (result != 1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "Failed to write to pipe: " << strerror(errno) << std::endl;
            }
        }
    }
}

int EvLoop::poll(int timeout_ms) {
    poll_fds_.clear();
    poll_fds_.reserve(fd_map_.size());

    for (const auto& pair : fd_map_) {
    if (pair.second->active) {
        poll_fds_.push_back({
            .fd = pair.first,
            .events = pair.second->events,
            .revents = 0
        });
    }
	}

    int actual_timeout = calculate_timeout(timeout_ms);

    if (poll_fds_.empty()) {
        return -1;
    }

    int result = ::poll(poll_fds_.data(), poll_fds_.size(), actual_timeout);

    if (result < 0) {
	if (errno == EINTR)
		return 0;
        perror("poll");
        return -1;
    }

    processing_loop_ = true;

    for (const auto& pfd : poll_fds_) {
        if (pfd.revents != 0) {
            auto it = fd_map_.find(pfd.fd);
            if (it != fd_map_.end()) {
                try {
                    it->second->callback(pfd.fd, pfd.events, pfd.revents);
                } catch (const std::exception& e) {
                    std::cerr << "Exception in fd callback for fd " << pfd.fd
                        << ": " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown exception in fd callback for fd " << pfd.fd << std::endl;
                }
            }
        }
    }

    processing_loop_ = false;

    for (auto it = fd_map_.begin(); it != fd_map_.end();) {
        if (!it->second->active) {
            it = fd_map_.erase(it);
        } else {
            ++it;
        }
    }

    process_timers();

    return result;
}

void EvLoop::run(int default_timeout_ms) {
    running_ = true;
    
    while (running_) {
        int result = poll(default_timeout_ms);

        if (result < 0 && errno != EINTR) {
            break;
        }
    }
}

void EvLoop::stop() {
    
    trigger_loop();
    running_ = false;
}

size_t EvLoop::get_fd_count() const {
    return fd_map_.size();
}

size_t EvLoop::get_timer_count() const {
    return timer_map_.size();
}

void EvLoop::timer_add_queue(std::shared_ptr<TimerInfo> timer) {
    auto it = timer_queue_.begin();
    while (it != timer_queue_.end() && (*it)->next_fire <= timer->next_fire) {
        ++it;
    }
    timer->updated = false;
    timer_queue_.insert(it, timer);
}

int EvLoop::add_timer(int interval_ms, TimerCallback callback, bool repeat) {
    if (interval_ms <= 0 || !callback) {
        return -1;
    }

    int timer_id = next_timer_id_++;
    auto now = std::chrono::steady_clock::now();
    auto interval = std::chrono::milliseconds(interval_ms);
    auto next_fire = now + interval;

    auto timer_info = std::make_shared<TimerInfo>(
        timer_id, std::move(callback), next_fire, interval, repeat);

    timer_map_[timer_id] = timer_info;
    timer_add_queue(timer_info);

    auto next_fire_ms = std::chrono::duration_cast<std::chrono::milliseconds>(next_fire.time_since_epoch()).count();
    dbg("timer added %d, next_fire %ld", timer_id, next_fire_ms);
    return timer_id;
}

bool EvLoop::update_timer_interval(int timer_id, int interval_ms) {
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

void EvLoop::process_timers() {
    auto now = std::chrono::steady_clock::now();
    processing_loop_ = true;
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

        try {
            timer_info->callback(timer_info->id);

            if (timer_info->repeat && timer_info->active) {
                timer_info->next_fire = now + timer_info->interval;
                timer_add_queue(timer_info);
            } else {
                timer_info->active = false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception in timer callback for timer " << timer_info->id
                << ": " << e.what() << std::endl;
            timer_info->active = false;
        } catch (...) {
            std::cerr << "Unknown exception in timer callback for timer " << timer_info->id << std::endl;
            timer_info->active = false;
        }
    }

    processing_loop_ = false;
    for (auto it = timer_map_.begin(); it != timer_map_.end();) {
        if (!it->second->active) {
            it = timer_map_.erase(it);
        } else {
            if (it->second->updated)
                timer_add_queue(it->second);
            ++it;
        }
    }
}

int EvLoop::calculate_timeout(int default_timeout_ms) const {
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

    dbg("next timer %d %ld", next_timer->id, time_to_next.count());
    if (time_to_next.count() <= 0) {
        return 0;
    }

    int timeout = static_cast<int>(time_to_next.count());

    if (default_timeout_ms < 0) {
        return timeout;
    }
    dbg("mintime %d", timeout);
    return std::min(timeout, default_timeout_ms);
}

bool EvLoop::remove_timer(int timer_id) {
    auto it = timer_map_.find(timer_id);
    if (it == timer_map_.end()) {
        return false;
    }
    dbg("timer deactived %d", timer_id);
    it->second->active = false;
    return true;
}

