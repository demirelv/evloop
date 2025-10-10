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
#include "poller.h"
#include "timer.h"

class EvLoop: public Timer, public Poller {
public:

    EvLoop();
    ~EvLoop();

    EvLoop(const EvLoop&) = delete;
    EvLoop& operator=(const EvLoop&) = delete;

    EvLoop(EvLoop&&) = default;
    EvLoop& operator=(EvLoop&&) = default;

    void run(int default_timeout_ms = 1000);

    void stop();

    int add_timer(int interval_ms, TimerCallback callback, bool repeat = true);

    bool remove_timer(int timer_id);

    bool update_timer_interval(int timer_id, int interval_ms);

 
};

