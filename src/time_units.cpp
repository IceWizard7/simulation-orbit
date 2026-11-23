#include "time_units.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <ctime>

#include "utils.hpp"

namespace {
    using namespace std::chrono;

    struct LeapStep {
        sys_days from;
        int tai_minus_utc{}; // seconds
    };

    // IERS Bulletin C; no new leap second since 2017-01-01
    constexpr std::array LEAP_SECONDS = {
        LeapStep{year{1972} / January / 1, 10}, LeapStep{year{1972} / July / 1, 11},
        LeapStep{year{1973} / January / 1, 12}, LeapStep{year{1974} / January / 1, 13},
        LeapStep{year{1975} / January / 1, 14}, LeapStep{year{1976} / January / 1, 15},
        LeapStep{year{1977} / January / 1, 16}, LeapStep{year{1978} / January / 1, 17},
        LeapStep{year{1979} / January / 1, 18}, LeapStep{year{1980} / January / 1, 19},
        LeapStep{year{1981} / July / 1, 20}, LeapStep{year{1982} / July / 1, 21},
        LeapStep{year{1983} / July / 1, 22}, LeapStep{year{1985} / July / 1, 23},
        LeapStep{year{1988} / January / 1, 24}, LeapStep{year{1990} / January / 1, 25},
        LeapStep{year{1991} / January / 1, 26}, LeapStep{year{1992} / July / 1, 27},
        LeapStep{year{1993} / July / 1, 28}, LeapStep{year{1994} / July / 1, 29},
        LeapStep{year{1996} / January / 1, 30}, LeapStep{year{1997} / July / 1, 31},
        LeapStep{year{1999} / January / 1, 32}, LeapStep{year{2006} / January / 1, 33},
        LeapStep{year{2009} / January / 1, 34}, LeapStep{year{2012} / July / 1, 35},
        LeapStep{year{2015} / July / 1, 36}, LeapStep{year{2017} / January / 1, 37},
    };

    double decimal_year(const sys_seconds t) {
        const year y = year_month_day{floor<days>(t)}.year();
        const sys_seconds start{sys_days{y / January / 1}};
        const sys_seconds next{sys_days{(y + years{1}) / January / 1}};
        return static_cast<int>(y) + duration<double>(t - start).count() / duration<double>(next - start).count();
    }

    // Espenak & Meeus (2006), "Five Millennium Canon of Solar Eclipses", NASA/TP-2006-214141
    double delta_t(const double y) {
        if (y < 1860) {
            const double t = y - 1800;
            return 13.72 - 0.332447 * t + 0.0068612 * t * t + 0.0041116 * std::pow(t, 3) - 0.00037436 * std::pow(t, 4)
                   + 0.0000121272 * std::pow(t, 5) - 0.0000001699 * std::pow(t, 6) + 0.000000000875 * std::pow(t, 7);
        }
        if (y < 1900) {
            const double t = y - 1860;
            return 7.62 + 0.5737 * t - 0.251754 * t * t + 0.01680668 * std::pow(t, 3)
                   - 0.0004473624 * std::pow(t, 4) + std::pow(t, 5) / 233174;
        }
        if (y < 1920) {
            const double t = y - 1900;
            return -2.79 + 1.494119 * t - 0.0598939 * t * t + 0.0061966 * std::pow(t, 3) - 0.000197 * std::pow(t, 4);
        }
        if (y < 1941) {
            const double t = y - 1920;
            return 21.20 + 0.84493 * t - 0.076100 * t * t + 0.0020936 * std::pow(t, 3);
        }
        if (y < 1961) {
            const double t = y - 1950;
            return 29.07 + 0.407 * t - t * t / 233 + std::pow(t, 3) / 2547;
        }
        const double t = y - 1975; // 1961–1972 (leap seconds take over from 1972)
        return 45.45 + 1.067 * t - t * t / 260 - std::pow(t, 3) / 718;
    }
}

double time_units::tt_minus_utc(const sys_seconds tt) {
    if (tt < sys_seconds{LEAP_SECONDS.front().from}) return delta_t(decimal_year(tt));

    int tai_minus_utc = LEAP_SECONDS.front().tai_minus_utc;
    for (const auto& step : LEAP_SECONDS) {
        if (tt >= sys_seconds{step.from}) tai_minus_utc = step.tai_minus_utc;
    }
    return 32.184 + tai_minus_utc; // TT = TAI + 32.184 s
}

str time_units::format_local_time(const double simulated_seconds) {
    const sys_seconds tt = sys_seconds{INITIAL_EPOCH} + round<seconds>(duration<double>(simulated_seconds)); // TDB ≈ TT
    const sys_seconds utc = tt - round<seconds>(duration<double>(tt_minus_utc(tt)));

    const std::time_t unix_time = system_clock::to_time_t(utc);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &unix_time);
#else
    localtime_r(&unix_time, &local); // OS time zone including DST and historical rules (LMT before standard time)
#endif

    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &local);
    return buffer;
}
