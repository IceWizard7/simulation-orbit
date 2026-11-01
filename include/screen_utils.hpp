#pragma once

#include <algorithm>
#include <cmath>
#include <raylib.h>

#include "config.hpp"
#include "vectors.hpp"

struct Viewport2D {
    double width;
    double height;
    double pixels_per_meter;
};

inline Viewport2D get_viewport_2d() {
    const double width = std::max(
        1.0, GetScreenWidth() - 2.0 * config::WINDOW_MARGIN
    );
    const double height = std::max(
        1.0, GetScreenHeight() - 2.0 * config::WINDOW_MARGIN
    );

    return {width, height, std::min(width, height) / (2.0 * config::SCALING)};
}

inline Vector2 to_raylib(const Vec3& v, const Vec2& offset_m) {
    const auto [width, height, pixels_per_meter] = get_viewport_2d();

    return {
        static_cast<float>(config::WINDOW_MARGIN + width * 0.5 + (v.x - offset_m.x) * pixels_per_meter),
        static_cast<float>(config::WINDOW_MARGIN + height * 0.5 + (v.y - offset_m.y) * pixels_per_meter),
    };
}

inline Vec2 screen_to_meters(const Vector2& screen, const Vec2& offset_m) {
    const auto [width, height, pixels_per_meter] = get_viewport_2d();

    return {
        offset_m.x + (
            screen.x - config::WINDOW_MARGIN - width * 0.5
        ) / pixels_per_meter,
        offset_m.y + (
            screen.y - config::WINDOW_MARGIN - height * 0.5
        ) / pixels_per_meter
    };
}

inline bool inside_screen(const Vec3& v, const Vec2& offset_m) {
    auto [x, y] = to_raylib(v, offset_m);

    return config::WINDOW_MARGIN <= x && x <= static_cast<float>(GetScreenWidth()) - config::WINDOW_MARGIN
           && config::WINDOW_MARGIN <= y && y <= static_cast<float>(GetScreenHeight()) - config::WINDOW_MARGIN;
}

inline bool circle_intersects_viewport(const Vector2 center, const double radius) {
    const double closest_x = std::clamp(
        static_cast<double>(center.x),
        static_cast<double>(config::WINDOW_MARGIN),
        static_cast<double>(GetScreenWidth() - config::WINDOW_MARGIN)
    );
    const double closest_y = std::clamp(
        static_cast<double>(center.y),
        static_cast<double>(config::WINDOW_MARGIN),
        static_cast<double>(GetScreenHeight() - config::WINDOW_MARGIN)
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
