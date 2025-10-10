#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include "poller.h"
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

void Poller::close_pipe() {
    if (pipe_fds[0] > 0) {
        remove(pipe_fds[0]);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        pipe_fds[0] = pipe_fds[1] = -1;
    }
}

Poller::Poller() : running_(true), processing_loop_(false) {
    poll_fds_.reserve(64);
    pipe_fds[0] = pipe_fds[1] = -1;
    create_pipe();
}

Poller::~Poller() {
    stop();
    close_pipe();
}

bool Poller::add(int fd, short events, FdCallback callback) {
    std::unique_lock<std::shared_mutex> lock(mtx);
    if (fd < 0 || !callback) {
        return false;
    }

    if (fd_map_.find(fd) != fd_map_.end()) {
        dbg("Warning: FD %d is already being watched", fd);
        return false;
    }
    fd_map_[fd] = std::make_unique<FdInfo>(std::move(callback), events);
    return true;
}

bool Poller::remove(int fd) {
    std::unique_lock<std::shared_mutex> lock(mtx);
    auto it = fd_map_.find(fd);
    if (it == fd_map_.end()) {
        return false;
    }
    it->second->active = false;
    return true;
}

bool Poller::update_events(int fd, short events) {
    std::unique_lock<std::shared_mutex> lock(mtx);
    auto it = fd_map_.find(fd);
    if (it == fd_map_.end()) {
        return false;
    }
    it->second->events = events;
    dbg("updated events fd %d, %04x", fd, events);
    return true;
}

bool Poller::create_pipe() {

    if (pipe(pipe_fds) != 0) {
        dbg("Failed to create pipe: %s", strerror(errno).c_str());
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

    if (!add(pipe_fds[0], POLLIN, callback)) {
        std::cerr << "Failed to add pipe to event loop" << std::endl;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        pipe_fds[0] = pipe_fds[1] = -1;
        return false;
    }

    return true;
}

void Poller::handle_pipe_callback(int fd, short events, short revents) {

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

void Poller::trigger_loop() const {
    std::unique_lock<std::shared_mutex> lock(mtx);
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

int Poller::poll(int timeout_ms) {
    std::vector<pollfd> poll_fds_;
    std::shared_lock<std::shared_mutex> lock(mtx);
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

    if (poll_fds_.empty()) {
        return -1;
    }

    int result = ::poll(poll_fds_.data(), poll_fds_.size(), timeout_ms);

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
                lock.unlock();
                try {
                    it->second->callback(pfd.fd, pfd.events, pfd.revents);
                } catch (const std::exception& e) {
                    std::cerr << "Exception in fd callback for fd " << pfd.fd
                        << ": " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown exception in fd callback for fd " << pfd.fd << std::endl;
                }
                lock.lock();
            }
        }
    }

    processing_loop_ = false;
    lock.unlock();
    {
        std::unique_lock<std::shared_mutex> wlock(mtx);
        for (auto it = fd_map_.begin(); it != fd_map_.end();) {
            if (!it->second->active) {
                it = fd_map_.erase(it);
            } else {
                ++it;
            }
        }
    }

    return result;
}

void Poller::run(int default_timeout_ms) {
    running_ = true;

    while (running_) {
        int result = poll(default_timeout_ms);

        if (result < 0 && errno != EINTR) {
            break;
        }
    }
}

void Poller::stop() {
    std::unique_lock<std::shared_mutex> lock(mtx);
    running_ = false;
    lock.unlock();
    trigger_loop();
}

bool Poller::running() const {
    return running_;
}

size_t Poller::get_fd_count() const {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return fd_map_.size();
}

