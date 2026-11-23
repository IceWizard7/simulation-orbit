#pragma once

#include <array>
#include <chrono>
#include <format>

#include "config.hpp"
#include "utils.hpp"

namespace time_units {
    struct TimeUnit {
        const char* suffix;
        double seconds;
    };

    // Inline needed: one shared array across translation units, so pointers to its elements stay valid
    inline constexpr std::array UNITS{
        TimeUnit{"s", 1.0},
        TimeUnit{"min", 60.0},
        TimeUnit{"h", 3'600.0},
        TimeUnit{"d", 86'400.0},
        TimeUnit{"yr", config::SECONDS_PER_YEAR}
    };

    // Largest unit that still fits at least twice (NaN and negative values fall back to seconds)
    inline const TimeUnit& choose(const double seconds) {
        const TimeUnit* chosen = &UNITS.front();

        for (const TimeUnit& unit : UNITS) {
            if (seconds >= 2.0 * unit.seconds) chosen = &unit;
        }

        return *chosen;
    }

    // Ex. 27554.4 s -> "7.654 h"
    inline str format_duration(const double seconds, const int significant_digits = 6) {
        const TimeUnit& unit = choose(std::abs(seconds));

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*g %s", significant_digits, seconds / unit.seconds, unit.suffix);
        return buffer;
    }

    // Epoch of the initial states (Horizons vectors, TDB). Single source for the date display and the JSON export
    inline constexpr std::chrono::sys_days INITIAL_EPOCH = std::chrono::year{1800} / std::chrono::January / 3;

    // Calendar date of the simulated time: Epoch + seconds on the proleptic Gregorian calendar (real leap years)
    // Independent of the 365-day project year, so it does not drift
    // TDB, not UTC (leap seconds / Delta T ignored)
    inline str format_epoch_date(const double simulated_seconds) {
        const auto t = INITIAL_EPOCH + std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::duration<double>(simulated_seconds));
        return std::format("{:%Y-%m-%d %H:%M} TDB", floor<std::chrono::minutes>(t));
    }

    // TT − UTC in seconds (~ Delta T = TT − UT1 before 1972): Espenak & Meeus (2006) polynomials 1800–1972, leap seconds after
    [[nodiscard]] double tt_minus_utc(std::chrono::sys_seconds tt);

    // Local wall-clock time of the simulated moment, to the second (TDB ~ TT -> UTC -> OS time zone)
    [[nodiscard]] str format_local_time(double simulated_seconds);
}
