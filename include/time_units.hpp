#pragma once

#include <array>

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
}