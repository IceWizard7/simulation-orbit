#pragma once

#include <raylib.h>

#include "config.hpp"
#include "vectors.hpp"

inline Vector2 to_raylib(const Vec3& v, const Vec2& offset_m) {
    // x in interval [0, WINDOW_WIDTH]
    // y in interval [0, WINDOW_HEIGHT]

    return {
        config::WINDOW_MARGIN + static_cast<float>((v.x - offset_m.x) / config::SCALING / 2.0 + 0.5) * (config::WINDOW_WIDTH - 2 * config::WINDOW_MARGIN),
        config::WINDOW_MARGIN + static_cast<float>((v.y - offset_m.y) / config::SCALING / 2.0 + 0.5) * (config::WINDOW_HEIGHT - 2 * config::WINDOW_MARGIN)
    };
}

inline Vec2 screen_to_meters(const Vector2& screen, const Vec2& offset_m) {
    constexpr double width = config::WINDOW_WIDTH - 2.0 * config::WINDOW_MARGIN;
    constexpr double height = config::WINDOW_HEIGHT - 2.0 * config::WINDOW_MARGIN;

    return {
        offset_m.x + (
            (screen.x - config::WINDOW_MARGIN) / width - 0.5
        ) * 2.0 * config::SCALING,

        offset_m.y + (
            (screen.y - config::WINDOW_MARGIN) / height - 0.5
        ) * 2.0 * config::SCALING
    };
}

inline bool inside_screen(const Vec3& v, const Vec2& offset_m) {
    auto [x, y] = to_raylib(v, offset_m);

    return config::WINDOW_MARGIN <= x && x <= config::WINDOW_WIDTH - config::WINDOW_MARGIN
           && config::WINDOW_MARGIN <= y && y <= config::WINDOW_HEIGHT - config::WINDOW_MARGIN;
}

inline bool circle_intersects_viewport(const Vector2 center, const double radius) {
    const double closest_x = std::clamp(
        static_cast<double>(center.x),
        static_cast<double>(config::WINDOW_MARGIN),
        static_cast<double>(config::WINDOW_WIDTH - config::WINDOW_MARGIN)
    );
    const double closest_y = std::clamp(
        static_cast<double>(center.y),
        static_cast<double>(config::WINDOW_MARGIN),
        static_cast<double>(config::WINDOW_HEIGHT - config::WINDOW_MARGIN)
    );

    return std::hypot(center.x - closest_x, center.y - closest_y) <= radius;
}

inline Vector3 to_world(const Vec3& m) { // meters
    return {
        static_cast<float>(m.x / config::WORLD_UNIT_METERS), // x
        static_cast<float>(m.z / config::WORLD_UNIT_METERS), // physics z -> raylib y (up)
        static_cast<float>(m.y / config::WORLD_UNIT_METERS), // physics y -> raylib z
    };
}

inline Vector3 to_world(const Vec3& position, const Vec3& origin) {
    // Subtraction happens in double precision (Vec3)
    return to_world(position - origin);
}
