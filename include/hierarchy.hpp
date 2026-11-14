#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "config.hpp"
#include "orbital_elements.hpp"

struct Hierarchy {
    std::array<std::optional<std::size_t>, NUM_CELESTIAL_BODIES> host; // empty = top level ("root")
    std::array<double, NUM_CELESTIAL_BODIES> hill_radius{}; // +inf for roots, NaN for disabled/negative-mass slots
};

// Does i orbit ancestor, directly or through intermediate hosts (ex. moon of a moon)?
bool orbits(const Hierarchy& h, std::size_t i, std::size_t ancestor);

// 0 = root, 1 = orbits a root (planet), 2 = moon, ...
std::size_t depth(const Hierarchy& h, std::size_t i);

Hierarchy compute_hierarchy(std::span<const std::optional<PointMass>> bodies);

// Everything currently orbiting planet (also moons of moons)
std::vector<std::size_t> members_of(const Hierarchy& h, std::size_t planet);

// Merging planet with its satellites removes their wobble from planet's orbit around reference
// No orbit to clean up for a root; a reference inside planet's own system can't be merged with it
bool can_merge(const Hierarchy& h, std::size_t planet, std::optional<std::size_t> reference);

// planet + members_of(...) as one mass-weighted point
PointMass system_barycenter(std::span<const std::optional<PointMass>> bodies, const Hierarchy& h, std::size_t planet);
