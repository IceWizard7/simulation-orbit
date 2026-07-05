#pragma once

#include <raylib.h>

#include "config.hpp"
#include "vectors.hpp"

inline Vector2 to_raylib(const Vec2& v) {
    // x in interval [0, WINDOW_WIDTH]
    // y in interval [0, WINDOW_HEIGHT]

    return {
        config::WINDOW_MARGIN + static_cast<float>((v.x / config::SCALING) / 2.0 + 0.5) * (config::WINDOW_WIDTH - 2 * config::WINDOW_MARGIN),
        config::WINDOW_MARGIN + static_cast<float>((v.y / config::SCALING) / 2.0 + 0.5) * (config::WINDOW_HEIGHT - 2 * config::WINDOW_MARGIN)
    };
}

inline Vector2 to_raylib(const Vec3& v) {
    // x in interval [0, WINDOW_WIDTH]
    // y in interval [0, WINDOW_HEIGHT]

    return {
        config::WINDOW_MARGIN + static_cast<float>((v.x / config::SCALING) / 2.0 + 0.5) * (config::WINDOW_WIDTH - 2 * config::WINDOW_MARGIN),
        config::WINDOW_MARGIN + static_cast<float>((v.y / config::SCALING) / 2.0 + 0.5) * (config::WINDOW_HEIGHT - 2 * config::WINDOW_MARGIN)
    };
}

static bool inside_screen(const Vec3 &vec3) {
    auto [x, y] = to_raylib(vec3);

    return (config::WINDOW_MARGIN <= x && x <= config::WINDOW_WIDTH - config::WINDOW_MARGIN
        && config::WINDOW_MARGIN <= y && y <= config::WINDOW_HEIGHT - config::WINDOW_MARGIN
    );
}
