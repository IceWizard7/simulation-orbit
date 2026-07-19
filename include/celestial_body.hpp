#pragma once

#include <optional>
#include <raylib.h>

#include "planet_visual.hpp"
#include "utils.hpp"
#include "vectors.hpp"

class CelestialBody {
public:
    const double original_gravitational_mass; // immutable GM of this explicit body
    double gravitational_mass; // effective GM; combined system GM in planet-systems mode
    const str name;
    Vec3 position;
    Vec3 velocity;
    const float draw_radius_2d;
    const float draw_radius_3d;
    const std::optional<Color> color;

    // Approximate orbital period (parent-relative for moons, heliocentric for planets;
    // the Sun uses Pluto's period so its history — which bounds how far back center-relative
    // trails can reach — spans every other body's full tail)
    // Rendering-only; sets the orbit-trail sampling cadence and tail length
    const double orbital_period_seconds;

    PlanetVisual planet_visual;
    bool enabled = true;

    // Oblateness (zonal J2) parameters. 0 -> spherical point masses (ex. sun, moons, Pluto)
    // set for oblate planets so their satellites feel the equatorial-bulge perturbation.
    const double j2; // dimensionless second zonal harmonic
    const double equatorial_radius; // meters, reference radius for the J2 term
    const Vec3 pole_axis; // unit spin axis in the simulation's J2000 ecliptic frame

    CelestialBody(
        str name,
        const Vec3 &position,
        const Vec3 &velocity,
        double gravitational_mass,
        float radius_2d,
        float radius_3d,
        const std::optional<Color>& color,
        double orbital_period_seconds,
        const std::optional<str>& texture_path = std::nullopt,
        double j2 = 0.0,
        double equatorial_radius = 0.0,
        const Vec3& pole_axis = {}
    );

    [[nodiscard]] double distance_to(const CelestialBody &body) const;

    [[nodiscard]] double get_mass() const;

    [[nodiscard]] Vec3 acceleration_due_to(const CelestialBody &source) const;
};
