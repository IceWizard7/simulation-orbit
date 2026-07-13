#include "celestial_body.hpp"

#include <utility>

#include "config.hpp"

CelestialBody::CelestialBody(
    str name,
    const Vec3 &position,
    const Vec3 &velocity,
    const double gravitational_mass,
    const float radius_2d,
    const float radius_3d,
    const std::optional<Color>& color,
    const int max_rendered_orbit_segments_per_body,
    const int max_rendered_orbit_tail,
    const std::optional<str>& texture_path,
    const double j2,
    const double equatorial_radius,
    const Vec3& pole_axis
)
: mass(gravitational_mass / config::GRAVITATIONAL_CONSTANT),
gravitational_mass(gravitational_mass),
name(std::move(name)),
position(position),
velocity(velocity),
draw_radius_2d(radius_2d),
draw_radius_3d(radius_3d),
color(color),
max_rendered_orbit_segments_per_body(max_rendered_orbit_segments_per_body),
max_rendered_orbit_tail(max_rendered_orbit_tail),
j2(j2),
equatorial_radius(equatorial_radius),
pole_axis(pole_axis) {
    planet_visual.texture_path = texture_path;
}

[[nodiscard]] double CelestialBody::distance_to(const CelestialBody &body) const {
    return (position - body.position).length();
}

[[nodiscard]] Vec3 CelestialBody::acceleration_due_to(const CelestialBody &source) const {
    const double distance = distance_to(source);
    const Vec3 direction = (source.position - position) / distance;

    // Newtonian point mass: (G * M / r^2) * direction
    Vec3 acceleration = direction * (source.gravitational_mass / (distance * distance));

    // Oblateness (zonal J2) perturbation from a non-spherical source
    // Ex. an oblate planet acting on its moons
    // The term falls off as 1/r^4, so it is only meaningful for close satellites and is automatically negligible for distant bodies
    // Vector form with spin axis k and r_hat from source to this:
    // a_J2 = -1.5 * J2 * GM * R_eq^2 / r^4 * [ (1 - 5*u^2) r_hat + 2*u*k ],  u = r_hat . k
    // Derivation: a = -grad(U) of the quadrupole-truncated external potential
    // U(r) = -GM/r * [ 1 - J2 (R_eq/r)^2 * P2(u) ],  P2(u) = (3*u^2 - 1)/2,
    // where u = cos(colatitude) = r_hat . k.

    // Sources:
    // Murray & Dermott, "Solar System Dynamics" (1999) sec. 6.11 (planetary oblateness / J2)
    // Montenbruck & Gill, "Satellite Orbits" (2000) sec. 3.2
    // Vallado, "Fundamentals of Astrodynamics and Applications" (4th ed.) sec. 8.6.4.

    // Sanity check, equatorial plane (u=0): a_J2 = -1.5*J2*GM*R_eq^2/r^4 * r_hat,
    // i.e. extra inward pull -> prograde apsidal precession, as expected.
    if (runtime_config::use_j2 && source.j2 != 0.0) {
        const Vec3 r_hat = direction * -1.0; // unit vector from source (planet) to this body
        const double u = r_hat.dot(source.pole_axis); // cos(colatitude) relative to the spin axis
        const double coefficient = -1.5 * source.j2 * source.gravitational_mass
            * source.equatorial_radius * source.equatorial_radius
            / (distance * distance * distance * distance);
        acceleration += (r_hat * (1.0 - 5.0 * u * u) + source.pole_axis * (2.0 * u)) * coefficient;
    }

    return acceleration;
}
