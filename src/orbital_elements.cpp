#include "orbital_elements.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include "config.hpp"
#include "vectors.hpp"

// Region where planet's gravity dominates over reference's tidal pull; NaN if reference isn't heavier
double hill_radius(const PointMass& planet, const PointMass& reference) {
    if (!(planet.mass > 0.0) || !(reference.mass > planet.mass)) return std::numeric_limits<double>::quiet_NaN();

    return (planet.position - reference.position).length() * std::cbrt(planet.mass / (3.0 * reference.mass));
}

// Is candidate currently a moon of planet? (relative to the dominant reference, ex. the Sun)
inline bool is_satellite(const PointMass& planet, const PointMass& candidate, const PointMass& reference) {
    if (!(candidate.mass > 0.0) || candidate.mass >= planet.mass || !(reference.mass > planet.mass)) return false;

    const Vec3 r = candidate.position - planet.position;
    const Vec3 v = candidate.velocity - planet.velocity;
    const double distance = r.length();

    // Hill sphere: region where the planet's gravity dominates over the reference's tidal pull
    const double hill = (planet.position - reference.position).length() * std::cbrt(planet.mass / (3.0 * reference.mass));
    if (!(distance > 0.0) || distance >= hill) return false;

    return 0.5 * v.dot(v) - config::GRAVITATIONAL_CONSTANT * (planet.mass + candidate.mass) / distance < 0.0;
}

// Planet + its current satellites as one mass-weighted point; empty slots = disabled bodies
PointMass system_barycenter(const std::span<const std::optional<PointMass>> bodies, const std::size_t planet, const std::size_t reference, std::vector<std::size_t>* members) {
    const PointMass& p = *bodies[planet];
    const PointMass& ref = *bodies[reference];

    Vec3 weighted_position = p.position * p.mass;
    Vec3 weighted_velocity = p.velocity * p.mass;
    double mass = p.mass;

    for (std::size_t c = 0; c < bodies.size(); c++) {
        if (c == planet || c == reference || !bodies[c] || !is_satellite(p, *bodies[c], ref)) continue;

        weighted_position += bodies[c]->position * bodies[c]->mass;
        weighted_velocity += bodies[c]->velocity * bodies[c]->mass;
        mass += bodies[c]->mass;
        if (members) members->push_back(c);
    }

    if (!(mass > 0.0)) return p;

    return {weighted_position / mass, weighted_velocity / mass, mass};
}

[[nodiscard]] bool OrbitalElements::is_finite() const {
    return std::isfinite(semi_major_axis) && std::isfinite(semi_minor_axis)
        && std::isfinite(linear_eccentricity) && std::isfinite(eccentricity)
        && std::isfinite(orbital_period) && std::isfinite(inclination);
}

std::optional<OrbitalElements> orbital_elements(const Vec3& r, const Vec3& v, const double mu) {
    constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

    const double distance = r.length();
    if (!(mu > 0.0) || !(distance > 0.0)) return std::nullopt; // Also rejects negative masses and NaN

    const Vec3 h = r.cross(v);
    const double h_length = h.length();
    const double energy = 0.5 * v.dot(v) - mu / distance;
    const bool bound = energy < 0.0;
    const double a = bound ? -mu / (2.0 * energy) : NaN;
    const double b = bound ? h_length * std::sqrt(a / mu) : NaN;

    const double eccentricity = (v.cross(h) / mu - r / distance).length(); // e (no unit)
    const double linear_eccentricity = bound ? a * eccentricity : NaN;     // c = a * e (m)

    return OrbitalElements{
        .semi_major_axis = a,
        .semi_minor_axis = b,
        .linear_eccentricity = linear_eccentricity,
        .eccentricity = eccentricity,
        .orbital_period = bound ? 2.0 * std::numbers::pi * std::sqrt(a * a * a / mu) : NaN,
        .inclination = h_length > 0.0 ? std::acos(std::clamp(h.z / h_length, -1.0, 1.0)) : NaN,
    };
}
