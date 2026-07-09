#pragma once

#include <optional>
#include <raylib.h>

#include "config.hpp"
#include "planet_visual.hpp"
#include "utils.hpp"
#include "vectors.hpp"

class CelestialBody {
private:
    double mass;
    double gravitational_mass; // mass * G
public:
    str name;
    Vec3 position;
    Vec3 velocity;
    float draw_radius_2d;
    float draw_radius_3d;
    std::optional<Color> color;
    int max_rendered_orbit_segments_per_body;
    int max_rendered_orbit_tail;
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

    [[nodiscard]] double get_mass() const {
        return mass;
    }

    [[nodiscard]] double get_gravitational_mass() const {
        return gravitational_mass;
    }

    void set_mass(const double mass_) {
        this->mass = mass_;
        this->gravitational_mass = mass * config::GRAVITATIONAL_CONSTANT;
    }
};
