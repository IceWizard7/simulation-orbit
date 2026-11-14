#include "hierarchy.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <vector>

bool orbits(const Hierarchy& h, std::size_t i, const std::size_t ancestor) {
    // Terminates: every host is strictly heavier, so the chain can't loop
    while (h.host[i]) {
        i = *h.host[i];
        if (i == ancestor) return true;
    }
    return false;
}

std::size_t depth(const Hierarchy& h, std::size_t i) {
    // Terminates: every host is strictly heavier, so the chain can't loop
    std::size_t d = 0;

    while (h.host[i]) {
        i = *h.host[i];
        d++;
    }

    return d;
}

Hierarchy compute_hierarchy(const std::span<const std::optional<PointMass>> bodies) {
    Hierarchy h;
    h.hill_radius.fill(std::numeric_limits<double>::quiet_NaN()); // Fill with disabled value (NaN)

    // Heaviest first; stable sort keeps index order on equal masses (deterministic)
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < bodies.size(); i++) {
        if (bodies[i] && bodies[i]->mass >= 0.0) order.push_back(i); // negative masses take no part
    }
    std::ranges::stable_sort(order, std::ranges::greater{}, [&](const std::size_t i) { return bodies[i]->mass; });

    for (std::size_t k = 0; k < order.size(); k++) {
        const std::size_t i = order[k];
        const PointMass& body = *bodies[i];

        // order[0..k) are heavier or equal and already have their Hill radius
        for (std::size_t c = 0; c < k; c++) {
            const std::size_t j = order[c];
            const PointMass& candidate_host = *bodies[j];

            // Do not change around orders of comparison operators; NaN has special behavior
            if (!(candidate_host.mass > body.mass)) continue;
            if (!((body.position - candidate_host.position).length() < h.hill_radius[j])) continue; // NaN -> skip

            if (!h.host[i] || h.hill_radius[j] < h.hill_radius[*h.host[i]]) {
                h.host[i] = j; // tightest wins
            }
        }

        h.hill_radius[i] = h.host[i] ? hill_radius(body, *bodies[*h.host[i]]) : std::numeric_limits<double>::infinity();
    }

    return h;
}

// Everything currently orbiting planet (also moons of moons)
std::vector<std::size_t> members_of(const Hierarchy& h, const std::size_t planet) {
    std::vector<std::size_t> members;
    for (std::size_t c = 0; c < h.host.size(); c++) {
        if (c != planet && orbits(h, c, planet)) members.push_back(c); // also skips disabled slots
    }
    return members;
}

bool can_merge(const Hierarchy& h, const std::size_t planet, const std::optional<std::size_t> reference) {
    return h.host[planet].has_value() && !(reference && orbits(h, *reference, planet));
}

// Planet + its current satellites as one mass-weighted point; empty slots = disabled bodies
PointMass system_barycenter(const std::span<const std::optional<PointMass>> bodies, const Hierarchy& h, const std::size_t planet) {
    const PointMass& p = *bodies[planet];

    Vec3 weighted_position = p.position * p.mass;
    Vec3 weighted_velocity = p.velocity * p.mass;
    double mass = p.mass;

    for (const std::size_t c: members_of(h, planet)) {
        weighted_position += bodies[c]->position * bodies[c]->mass;
        weighted_velocity += bodies[c]->velocity * bodies[c]->mass;
        mass += bodies[c]->mass;
    }

    if (!(mass > 0.0)) return p;

    return {weighted_position / mass, weighted_velocity / mass, mass};
}
