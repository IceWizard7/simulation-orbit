#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "vectors.hpp"

struct PointMass {
    Vec3 position, velocity;
    double mass = 0.0;
};

double hill_radius(const PointMass& planet, const PointMass& reference);

bool is_satellite(const PointMass& planet, const PointMass& candidate, const PointMass& reference);

PointMass system_barycenter(std::span<const std::optional<PointMass>> bodies, std::size_t planet, std::size_t reference, std::vector<std::size_t>* members = nullptr);

// Osculating two-body elements of r, v relative to a reference, with mu = G * (M_ref + m_body)
struct OrbitalElements {
    double semi_major_axis;     // a (m), NaN if unbound
    double semi_minor_axis;     // b (m), NaN if unbound
    double linear_eccentricity; // c = a * e (m), NaN if unbound
    double eccentricity;        // e (no unit)
    double orbital_period;      // T (s), NaN if unbound
    double inclination;         // i (rad) against the stored ecliptic plane

    [[nodiscard]] bool is_finite() const;
};

std::optional<OrbitalElements> orbital_elements(const Vec3& r, const Vec3& v, double mu);
