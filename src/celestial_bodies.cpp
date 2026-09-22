#include "celestial_bodies.hpp"

#include "config.hpp"

template<std::size_t NUM_CELESTIAL_BODIES>
CelestialBodies<NUM_CELESTIAL_BODIES>::CelestialBodies(std::array<CelestialBodyInit, NUM_CELESTIAL_BODIES> bodies) {
    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        original_gravitational_masses[i] = bodies[i].gravitational_mass;
        gravitational_masses[i] = bodies[i].gravitational_mass;
        names[i] = bodies[i].name;

        pos_x[i] = bodies[i].position.x;
        pos_y[i] = bodies[i].position.y;
        pos_z[i] = bodies[i].position.z;

        vel_x[i] = bodies[i].velocity.x;
        vel_y[i] = bodies[i].velocity.y;
        vel_z[i] = bodies[i].velocity.z;

        draw_radii_2d[i] = bodies[i].radius_2d;
        draw_radii_3d[i] = bodies[i].radius_3d;
        colors[i] = bodies[i].color;

        orbital_period_seconds[i] = bodies[i].orbital_period_seconds;

        enabled[i] = true;

        j2[i] = bodies[i].j2;
        equatorial_radii[i] = bodies[i].equatorial_radius;
        pole_axes[i] = bodies[i].pole_axis;

        planet_visuals[i].texture_path = bodies[i].texture_path;
    }
}

template<std::size_t NUM_CELESTIAL_BODIES>
 Vec3 CelestialBodies<NUM_CELESTIAL_BODIES>::position(const std::size_t i) const {
    return {pos_x[i], pos_y[i], pos_z[i]};
}

template<std::size_t NUM_CELESTIAL_BODIES>
void CelestialBodies<NUM_CELESTIAL_BODIES>::set_position(const std::size_t i, const Vec3& position) {
    pos_x[i] = position.x;
    pos_y[i] = position.y;
    pos_z[i] = position.z;
}

template<std::size_t NUM_CELESTIAL_BODIES>
Vec3 CelestialBodies<NUM_CELESTIAL_BODIES>::velocity(const std::size_t i) const {
    return {vel_x[i], vel_y[i], vel_z[i]};
}


template<std::size_t NUM_CELESTIAL_BODIES>
void CelestialBodies<NUM_CELESTIAL_BODIES>::set_velocity(const std::size_t i, const Vec3& velocity) {
    vel_x[i] = velocity.x;
    vel_y[i] = velocity.y;
    vel_z[i] = velocity.z;
}

template<std::size_t NUM_CELESTIAL_BODIES>
double CelestialBodies<NUM_CELESTIAL_BODIES>::distance_to(const std::size_t i, const std::size_t to) const {
    return (position(i) - position(to)).length();
}

template<std::size_t NUM_CELESTIAL_BODIES>
double CelestialBodies<NUM_CELESTIAL_BODIES>::get_mass(const std::size_t i) const {
    return gravitational_masses[i] / config::GRAVITATIONAL_CONSTANT;
}

template<std::size_t NUM_CELESTIAL_BODIES>
Vec3 CelestialBodies<NUM_CELESTIAL_BODIES>::j2_acceleration_at(
    std::size_t i,
    const Vec3& source_to_target_direction,
    const double distance
) const {
    // Oblateness (zonal J2) perturbation from a non-spherical source
    // Ex. an oblate planet acting on its moons
    // The term falls off as 1/r^4, so it is only meaningful for close satellites and is automatically negligible for distant bodies
    // Vector form with spin axis k and r_hat from this source to the target:
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
    if (j2[i] == 0.0) return {};

    // TODO: We may need to optimize this for SIMD too? Idk.

    const double u = source_to_target_direction.dot(pole_axes[i]);
    const double coefficient = -1.5 * j2[i] * gravitational_masses[i]
        * equatorial_radii[i] * equatorial_radii[i]
        / (distance * distance * distance * distance);
    return (source_to_target_direction * (1.0 - 5.0 * u * u) + pole_axes[i] * (2.0 * u))
        * coefficient;
}

// Prevents linker errors, because now the implementation for exactly NUM_CELESTIAL_BODIES is visible
template class CelestialBodies<NUM_CELESTIAL_BODIES>;
