#include "orbital_elements.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

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

    return 0.5 * v.dot(v) - mu / distance < 0.0;
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
    const double linear_eccentricity = bound ? a * eccentricity : NaN; // c = a * e (m)

    return OrbitalElements{
        .semi_major_axis     = a,
        .semi_minor_axis     = b,
        .linear_eccentricity = linear_eccentricity,
        .eccentricity        = eccentricity,
        .orbital_period      = bound ? 2.0 * std::numbers::pi * std::sqrt(a * a * a / mu) : NaN,
        .inclination         = h_length > 0.0 ? std::acos(std::clamp(h.z / h_length, -1.0, 1.0)) : NaN,
    };
}

// orbital_elements.cpp
CircularOrbit circular_orbit_velocity(const Vec3& r_rel, const double mu) {
    // Model: isolated two-body problem (host M, body m), relative coordinates r = r_body - r_host
    // Definitions:
    // µ = G * (M + m) (reduced two-body problem: r'' = -µ r / |r|^3)
    // k = (0, 0, 1)   ecliptic pole of the stored J2000 frame
    //
    // Circular orbit: gravity supplies exactly the centripetal acceleration
    // |v|^2 / r = µ / r^2  => |v| = sqrt(µ / r)
    //
    // Direction: perpendicular to r and prograde (counterclockwise seen from +z)
    // t = k x r = (-r_y, r_x, 0), v = sqrt(µ / r) * t / |t|
    // If r has a z component, the orbit is inclined; its plane contains r and t
    //
    // Degenerate cases:
    // - r = 0: coincident bodies, no orbit
    // - r parallel to k (r_x = r_y = 0): t = 0, direction undefined
    // - µ <= 0 (zero/negative test masses are allowed): no circular solution
    //
    // Note: A circular two-body initial state is not a circular N-body orbit;
    // Perturbations (ex. the Sun's tide on an Earth satellite) change it over time

    const double r = r_rel.length();
    const Vec3 t{-r_rel.y, r_rel.x, 0.0};
    const double t_length = t.length();

    if (r == 0.0) return {.issue = CircularOrbitIssue::coincident};
    if (t_length == 0.0) return {.issue = CircularOrbitIssue::pole_axis};
    if (!(mu > 0.0) || !std::isfinite(mu)) return {.issue = CircularOrbitIssue::non_positive_mu}; // also NaN

    return {.velocity = t * (std::sqrt(mu / r) / t_length)};
}

std::optional<ConicSectionPath> sample_conic_section(const Vec3& r, const Vec3& v, const double mu, const std::size_t count, const double max_distance) {
    const double r0 = r.length();
    if (!(mu > 0.0) || !std::isfinite(mu) || !(r0 > 0.0) || count < 2) return std::nullopt;

    const Vec3 h = r.cross(v);
    const double h_len = h.length();
    const double energy = 0.5 * v.dot(v) - mu / r0;
    if (!(h_len > 0.0) || !std::isfinite(energy) || energy == 0.0) return std::nullopt; // Radial / Parabolic

    const Vec3 e_vec = v.cross(h) / mu - r / r0;
    const double e = e_vec.length();
    constexpr double CIRCULAR_EPS = 1e-10;
    const bool circular = e < CIRCULAR_EPS;

    const Vec3 P = circular ? r / r0 : e_vec / e;
    const Vec3 Q = (h / h_len).cross(P);

    ConicSectionPath path;
    path.samples.reserve(count);

    if (energy < 0.0) {
        // Ellipse
        const double a = -mu / (2.0 * energy);
        const double b = a * std::sqrt(std::max(0.0, 1.0 - e * e));
        const double n = std::sqrt(mu / (a * a * a));
        const double E0 = circular
                              ? 0.0
                              : std::atan2(r.dot(v) / (e * std::sqrt(mu * a)), (1.0 - r0 / a) / e);
        const double M0 = E0 - e * std::sin(E0);

        for (std::size_t i = 0; i < count; i++) {
            const double E = E0 + 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(count - 1);
            path.samples.push_back({
                .position = P * (a * (std::cos(E) - e)) + Q * (b * std::sin(E)),
                .time     = (E - e * std::sin(E) - M0) / n,
            });
        }
        path.bound = true;
        path.orbital_period = 2.0 * std::numbers::pi / n;
    } else {
        // Hyperbola (energy > 0, e > 1)
        const double A = mu / (2.0 * energy);
        const double n = std::sqrt(mu / (A * A * A)); // hyperbolic "mean motion" (1/s)
        const double k = std::sqrt(e * e - 1.0); // = b/A
        const double H0 = std::asinh(r.dot(v) / (e * std::sqrt(mu * A)));
        const double M0 = e * std::sinh(H0) - H0;
        const double H_end = std::max(H0, std::acosh(std::max(1.0, (max_distance / A + 1.0) / e)));

        for (std::size_t i = 0; i < count; i++) {
            const double s = static_cast<double>(i) / static_cast<double>(count - 1);
            const double H = H0 + (H_end - H0) * s;
            path.samples.push_back({
                .position = P * (A * (e - std::cosh(H))) + Q * (A * k * std::sinh(H)),
                .time     = (e * std::sinh(H) - H - M0) / n,
            });
        }
        path.bound = false; // period stays NaN
    }

    return path;
}

std::vector<ConicSectionSample> conic_section_ticks(const ConicSectionPath& path, const double interval) {
    std::vector<ConicSectionSample> ticks;
    if (!(interval > 0.0) || path.samples.size() < 2) return ticks;

    double next = interval; // Skip t = 0 (the ghost itself)
    for (std::size_t j = 1; j < path.samples.size(); j++) {
        const auto& a = path.samples[j - 1];
        const auto& b = path.samples[j];

        while (next <= b.time && next < path.samples.back().time) {
            // Last point of an ellipse == start
            const double s = (next - a.time) / (b.time - a.time);
            ticks.push_back({.position = a.position + (b.position - a.position) * s, .time = next});
            next += interval;
        }
    }
    return ticks;
}
