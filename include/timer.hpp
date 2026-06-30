#pragma once

#include <atomic>
#include <chrono>

class PausableTimer {
    using Clock = std::chrono::steady_clock;

    Clock::time_point last_start = Clock::now();
    Clock::duration elapsed{};

    std::atomic<bool> running = true;
public:
    bool is_running() {return running;}

    void pause() {
        if (!running) return;

        elapsed += Clock::now() - last_start;
        running = false;
    }

    void resume() {
        if (running) return;
        last_start = Clock::now();
        running = true;
    }

    void resume_or_pause() {
        if (running) pause();
        else resume();
    }

    double seconds() const {
        auto total = elapsed;

        if (running) total += Clock::now() - last_start;

        return std::chrono::duration<double>(total).count();
    }
};
