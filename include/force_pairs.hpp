#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "config.hpp"

struct PlanetarySystem;

struct BodyPair {
    std::size_t first_index;
    std::size_t second_index;
};

namespace force_pairs {
    constexpr std::size_t TOTAL_PAIRS = NUM_CELESTIAL_BODIES * (NUM_CELESTIAL_BODIES - 1) / 2;

    inline std::vector<BodyPair> all_pairs = {};
    inline std::vector<BodyPair> active_pairs = {};

    inline std::size_t recorded_accelerations = 0;

    void initialize_all_pairs(const CelestialBody (&celestial_bodies)[NUM_CELESTIAL_BODIES]);

    bool is_sun(std::size_t body_index);
    bool is_moon(std::size_t body_index, const std::array<PlanetarySystem, 7> &planetary_systems);
    bool is_primary(std::size_t body_index, const std::array<PlanetarySystem, 7> &planetary_systems);
    bool have_same_parent(std::size_t body_index1, std::size_t body_index2, const std::array<PlanetarySystem, 7>& planetary_systems);
    bool is_parent_moon_pair(std::size_t body_index1, std::size_t body_index2, const std::array<PlanetarySystem, 7>& planetary_systems);
    bool belong_to_same_planetary_system(std::size_t body_index1, std::size_t body_index2, const std::array<PlanetarySystem, 7>& planetary_systems);

    void set_interaction_set(const std::array<PlanetarySystem, 7>& planetary_systems);

    std::size_t active_pair_count();
    std::size_t skipped_pair_count();

    void record_acceleration_evaluation();
    std::size_t acceleration_evaluation_count();
    std::size_t pair_evaluation_count();
}
