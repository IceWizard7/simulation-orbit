#pragma once

#include <raylib.h>

#include "utils.hpp"

namespace config {
    constexpr double ORIGINAL_SCALING = 8e12;
    constexpr double ORIGINAL_AXIS_SCALING = 8;

    constexpr double ZOOM_FACTOR = 1.0717734625362931; // n-th root of 10 works, because then ZOOM_FACTOR**n = 10 => near perfect zoom cycle
    constexpr double TIME_STEP = 900;
    constexpr str TIME_STEP_STRING = "5 mins";
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

    constexpr double WORLD_UNIT_METERS = 1e12;

    constexpr float DEFAULT_CAM_AZIMUTH = 45.0f * DEG2RAD;
    constexpr float DEFAULT_CAM_ELEVATION = 35.0f * DEG2RAD;
    constexpr float DEFAULT_CAM_DISTANCE = 25.0f;
    constexpr bool DEFAULT_VIEW_3D = false;

    constexpr float DRAG_SENSITIVITY = 0.01;
    constexpr float ZOOM_3D_FACTOR = 1.0717734625362931;

    constexpr double SECONDS_PER_YEAR = 86'400 * 365;

    constexpr double TARGET_TOTAL_SIMULATION_TIME = 200; // years; <= 0 unlimited
    constexpr double TARGET_SIMULATION_SPEED = -1; // years per second; <= 0 unlimited
    constexpr double TARGET_STEPS_PER_SECOND = TARGET_SIMULATION_SPEED * SECONDS_PER_YEAR / TIME_STEP;

    constexpr int TARGET_FPS = 60;
}
