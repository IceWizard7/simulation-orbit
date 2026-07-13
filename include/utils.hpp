#pragma once

#include <string>
#include <vector>

using str = std::string;

str to_power_of10(double x);

str round_to_hundreds(double x);

str get_numerical_suffix(int x);

namespace Vector_Utils {
    template <typename T>
    bool contains(T val, const std::vector<T> &vec);
}
