#pragma once

#include <optional>

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
    double semi_major_axis;     // a (m), NaN if unbound
    double semi_minor_axis;     // b (m), NaN if unbound
    double linear_eccentricity; // c = a * e (m), NaN if unbound
    double eccentricity;        // e (no unit)
    double orbital_period;      // T (s), NaN if unbound
    double inclination;         // i (rad) against the stored ecliptic plane

    [[nodiscard]] bool is_finite() const;
};

std::optional<OrbitalElements> orbital_elements(const Vec3& r, const Vec3& v, double mu);
