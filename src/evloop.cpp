#include "evloop.h"
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
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

EvLoop::EvLoop() {

}

EvLoop::~EvLoop() {
    stop();
}

void EvLoop::run(int default_timeout_ms) {
    while (running()) {
        int timeout = calculate_timeout(default_timeout_ms);

        int result = poll(timeout);
        if (result < 0 && errno != EINTR) {
            break;
        }
        process_timers();
    }
}

void EvLoop::stop() {
    this->Poller::stop();
}

int EvLoop::add_timer(int interval_ms, TimerCallback callback, bool repeat) {
int  ret = this->Timer::add_timer(interval_ms, callback, repeat);
    trigger_loop();
    return ret;
}

bool EvLoop::remove_timer(int timer_id) {
bool ret = this->Timer::remove_timer(timer_id);
    trigger_loop();
    return ret;
}

bool EvLoop::update_timer_interval(int timer_id, int interval_ms) {
bool ret = this->Timer::update_timer_interval(timer_id, interval_ms);
    trigger_loop();
    return ret;
}

