#pragma once

#include "utilities.hpp"

namespace config {
    constexpr double ORIGINAL_SCALING = 8e12;
    constexpr double ORIGINAL_AXIS_SCALING = 8;

    constexpr double ZOOM_FACTOR = 1.0717734625362931; // n-th root of 10 works, because then ZOOM_FACTOR**n = 10 => near perfect zoom cycle
    constexpr double TIME_STEP = 86'400;
    constexpr str TIME_STEP_STRING = "24 hrs";
    constexpr int MAX_ORBIT_POINTS = 100'000; // => ~22.9 MiB RAM for orbit_history
    constexpr int ORBIT_SAMPLE_EVERY_SECONDS = 259'200;
    constexpr bool RENDERING_COORDINATES_RELATIVE_TO_OBJECT = true;

    constexpr double GRAVITATIONAL_CONSTANT = 6.6743e-11;

    constexpr int WINDOW_HEIGHT = 900;
    constexpr int WINDOW_WIDTH = 900;

    constexpr int GRID_SPACING = 90;
    constexpr int WINDOW_MARGIN = 1 * GRID_SPACING;

    inline double SCALING = ORIGINAL_SCALING;
    inline double AXIS_SCALING = ORIGINAL_AXIS_SCALING;
}
