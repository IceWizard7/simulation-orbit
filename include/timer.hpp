#pragma once

#include <chrono>
#include <mutex>

class PausableTimer {
    using Clock = std::chrono::steady_clock;

    mutable std::mutex mtx;
    Clock::time_point last_start = Clock::now();
    Clock::duration elapsed{};
    bool running = false;

public:
    bool is_running() const {
        std::lock_guard lock(mtx);
        return running;
    }

    void pause() {
        std::lock_guard lock(mtx);
        if (!running) return;

        elapsed += Clock::now() - last_start;
        running = false;
    }

    void resume() {
        std::lock_guard lock(mtx);
        if (running) return;
        last_start = Clock::now();
        running = true;
    }

    void resume_or_pause() {
        std::lock_guard lock(mtx);
        if (running) {
            elapsed += Clock::now() - last_start;
            running = false;
        } else {
            last_start = Clock::now();
            running = true;
        }
    }

    double seconds() const {
        std::lock_guard lock(mtx);
        auto total = elapsed;

        if (running) total += Clock::now() - last_start;

        return std::chrono::duration<double>(total).count();
    }
};
