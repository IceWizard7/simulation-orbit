#include "utils.hpp"

#include <cmath>
#include <format>

using str = std::string;

str to_power_of10(const double x) {
    if (x == 0) return "0";

    const int exponent = static_cast<int>(std::floor(std::log10(std::fabs(x))));
    const double mantissa = x / std::pow(10.0, exponent);
    const str mantissaStr =
        std::fabs(mantissa - std::round(mantissa)) < 1e-9
            ? std::to_string(static_cast<long long>(std::round(mantissa)))
            : std::to_string(mantissa);

    if (std::fabs(mantissa - 1.0) < 1e-7) return "10^" + std::to_string(exponent);

    return mantissaStr + " x 10^" + std::to_string(exponent);
}

str round_to_hundreds(const double x) {
    str result = std::format("{:.2f}", x);

    if (const auto dot = result.find('.'); dot == str::npos) {
        return result;
    }

    // Pop extra zeros at the end (numbers without a dot don't reach this point)
    while (!result.empty() && result.back() == '0') {
        result.pop_back();
    }

    // Pop extra dots at the end
    if (!result.empty() && result.back() == '.') {
        result.pop_back();
    }

    // Avoid displaying "-0"
    if (result == "-0") {
        return "0";
    }

    return result;
}

str get_numerical_suffix(const int x) {
    if (x % 100 / 10 == 1) return "th";

    switch (x % 10) {
        case 1: return "st";
        case 2: return "nd";
        case 3: return "rd";
        default: return "th";
    }
}
