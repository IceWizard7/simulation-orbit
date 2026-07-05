#pragma once

#include <raylib.h>

#include "config.hpp"
#include "vectors.hpp"

inline Vector3 to_world(const Vec3& m) { // meters
    return {
        static_cast<float>(m.x / config::WORLD_UNIT_METERS), // x
        static_cast<float>(m.z / config::WORLD_UNIT_METERS), // physics z -> up
        static_cast<float>(m.y / config::WORLD_UNIT_METERS), // physics y -> raylib z
    };
}
