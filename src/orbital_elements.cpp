#include "orbital_elements.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

#include "config.hpp"
#include "vectors.hpp"

// Radius around body where its gravity beats host's tidal pull; NaN unless host is heavier and body.mass > 0
double hill_radius(const PointMass& body, const PointMass& host) {
    if (!(body.mass > 0.0) || !(host.mass > body.mass)) return std::numeric_limits<double>::quiet_NaN();

    return (body.position - host.position).length() * std::cbrt(body.mass / (3.0 * host.mass));
}

bool is_bound(const PointMass& host, const PointMass& body) {
    const Vec3 r = body.position - host.position;
    const Vec3 v = body.velocity - host.velocity;
    const double distance = r.length();
    const double mu = config::GRAVITATIONAL_CONSTANT * (host.mass + body.mass);

    if (!(distance > 0.0) || !(mu > 0.0)) return false; // coincident, negative/zero total mass, NaN

    return (0.5 * v.dot(v) - mu / distance) < 0.0;
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
