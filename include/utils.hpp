#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using step_t = std::uint64_t; // Step counts and indices: 64-bit on every platform (size_t is 32-bit on wasm32)
using str = std::string;
using str_view = std::string_view;

static_assert(std::atomic<step_t>::is_always_lock_free, "step_t (steps_simulated) must always stay lock-free");

str to_power_of10(double x);

str round_to_hundreds(double x);

str get_numerical_suffix(std::size_t x);

[[nodiscard]] inline bool is_blank(const str_view s) {
    return s.find_first_not_of(" \t\r\n") == str_view::npos;
}
