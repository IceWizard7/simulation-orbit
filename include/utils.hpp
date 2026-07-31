#pragma once

#include <cstddef>
#include <string>
#include <vector>

using str = std::string;

str to_power_of10(double x);

str round_to_hundreds(double x);

str get_numerical_suffix(std::size_t x);

namespace Vector_Utils {
    template <typename T>
    bool contains(T val, const std::vector<T> &vec) {
        for (const auto &v : vec) {
            if (v == val) return true;
        }
        return false;
    }
}
