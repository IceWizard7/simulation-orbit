#pragma once

#include <limits>
#include <optional>
#include <vector>

#include "vectors.hpp"

struct PointMass {
    Vec3 position, velocity;
    double mass = 0.0;
};

// Radius around body where its gravity beats host's tidal pull; NaN unless host is heavier and body.mass > 0
double hill_radius(const PointMass& body, const PointMass& host);

// Negative two-body energy: body can't escape host on its own
bool is_bound(const PointMass& host, const PointMass& body);

// Osculating two-body elements of r, v relative to a reference, with mu = G * (M_ref + m_body)
struct OrbitalElements {
    double semi_major_axis; // a (m), NaN if unbound
    double semi_minor_axis; // b (m), NaN if unbound
    double linear_eccentricity; // c = a * e (m), NaN if unbound
    double eccentricity; // e (no unit)
    double orbital_period; // T (s), NaN if unbound
    double inclination; // i (rad) against the stored ecliptic plane

    [[nodiscard]] bool is_finite() const;
};

std::optional<OrbitalElements> orbital_elements(const Vec3& r, const Vec3& v, double mu);

enum class CircularOrbitIssue {
    none, coincident, pole_axis, non_positive_mu
};

struct CircularOrbit {
    Vec3 velocity{}; // Relative to the host, m/s
    CircularOrbitIssue issue = CircularOrbitIssue::none;
};

// Prograde circular velocity about the ecliptic pole for a body at r_rel (relative to its host)
CircularOrbit circular_orbit_velocity(const Vec3& r_rel, double mu);

struct ConicSectionSample {
    Vec3 position; // Relative to the host, m
    double time{}; // Seconds from now (increasing and >= 0)
};

struct ConicSectionPath {
    std::vector<ConicSectionSample> samples;
    bool bound = false;
    double orbital_period = std::numeric_limits<double>::quiet_NaN(); // NaN if unbound
};

// Two-body (Kepler) path from the current state forward in time
// Ellipse: 1 full revolution; Hyperbola: until max_distance
// std::nullopt: mu <= 0, r = 0, radial (h = 0), parabolic, non-finite
std::optional<ConicSectionPath> sample_conic_section(const Vec3& r, const Vec3& v, double mu, std::size_t count,
                                                     double max_distance);

// Points at equal time steps along the path (linear interpolation between samples)
std::vector<ConicSectionSample> conic_section_ticks(const ConicSectionPath& path, double interval);
