#pragma once

#include <optional>
#include <raylib.h>

#include "planet_visual.hpp"
#include "utils.hpp"
#include "vectors.hpp"

class CelestialBody {
public:
    const double mass;
    const double gravitational_mass; // mass * G
    const str name;
    Vec3 position;
    Vec3 velocity;
    const float draw_radius_2d;
    const float draw_radius_3d;
    const std::optional<Color> color;
    const int max_rendered_orbit_segments_per_body;
    const int max_rendered_orbit_tail;
    PlanetVisual planet_visual;

    CelestialBody(
        str name,
        const Vec3 &position,
        const Vec3 &velocity,
        double gravitational_mass,
        float radius_2d,
        float radius_3d,
        const std::optional<Color>& color,
        int max_rendered_orbit_segments_per_body,
        int max_rendered_orbit_tail,
        const std::optional<str>& texture_path = std::nullopt
    );

    [[nodiscard]] double distance_to(const CelestialBody &body) const;

    [[nodiscard]] Vec3 acceleration_due_to(const CelestialBody &source) const;
};
