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
    const std::optional<str>& texture_path
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
max_rendered_orbit_tail(max_rendered_orbit_tail) {
    planet_visual.texture_path = texture_path;
}

[[nodiscard]] double CelestialBody::distance_to(const CelestialBody &body) const {
    return (position - body.position).length();
}

[[nodiscard]] Vec3 CelestialBody::acceleration_due_to(const CelestialBody &source) const {
    const double distance = distance_to(source);
    const Vec3 direction = (source.position - position) / distance;

    // (G * M / r^2) * direction
    return direction * (source.gravitational_mass / (distance * distance));
}
