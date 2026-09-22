#include "force_pairs.hpp"

#include <cstddef>
#include <optional>

#include "config.hpp"
#include "simulation.hpp"

void force_pairs::initialize_all_pairs(const CelestialBodies<NUM_CELESTIAL_BODIES>& celestial_bodies) {
    // Call after celestial bodies have been enabled / disabled accordingly

    all_pairs.clear();
    recorded_accelerations = 0;

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (!celestial_bodies.enabled[i]) continue;

        for (std::size_t j = i + 1; j < NUM_CELESTIAL_BODIES; j++) {
            if (!celestial_bodies.enabled[j]) continue;
            all_pairs.emplace_back(i, j);
        }
    }
}

bool force_pairs::is_sun(const std::size_t body_index) {
    return body_index == 0;
}

bool force_pairs::is_moon(const std::size_t body_index, const std::array<PlanetarySystem, 7> &planetary_systems) {
    for (const auto& [parent_index, moon_indices] : planetary_systems) {
        for (const auto& moon_index : moon_indices) {
            if (moon_index == body_index) return true;
        }
    }
    return false;
}

bool force_pairs::is_primary(const std::size_t body_index, const std::array<PlanetarySystem, 7> &planetary_systems) {
    // Sun, planet or dwarf planet
    return !is_moon(body_index, planetary_systems);
}

bool force_pairs::have_same_parent(const std::size_t body_index1, const std::size_t body_index2, const std::array<PlanetarySystem, 7> &planetary_systems) {
    if (is_primary(body_index1, planetary_systems)) return false;
    if (is_primary(body_index2, planetary_systems)) return false;

    // -> Both are moons

    std::optional<std::size_t> parent_index1;
    std::optional<std::size_t> parent_index2;

    for (const auto& [parent_index, moon_indices] : planetary_systems) {
        for (const auto& moon_index : moon_indices) {
            if (moon_index == body_index1) parent_index1 = parent_index;
            if (moon_index == body_index2) parent_index2 = parent_index;
        }
    }

    return parent_index1.has_value() && parent_index2.has_value() && *parent_index1 == *parent_index2;
}

bool force_pairs::is_parent_moon_pair(const std::size_t body_index1, const std::size_t body_index2, const std::array<PlanetarySystem, 7> &planetary_systems) {
    // Either of the indices could be the parent of the other

    for (const auto& [parent_index, moon_indices] : planetary_systems) {
        for (const auto& moon_index : moon_indices) {
            if (body_index1 == moon_index && body_index2 == parent_index) return true;
            if (body_index1 == parent_index && body_index2 == moon_index) return true;
        }
    }

    return false;
}

bool force_pairs::belong_to_same_planetary_system(const std::size_t body_index1, const std::size_t body_index2, const std::array<PlanetarySystem, 7> &planetary_systems) {
    // Checks if both are part of the same planetary system

    std::optional<std::size_t> planetary_index1;
    std::optional<std::size_t> planetary_index2;

    for (const auto& [parent_index, moon_indices] : planetary_systems) {
        if (parent_index == body_index1) planetary_index1 = parent_index;
        if (parent_index == body_index2) planetary_index2 = parent_index;
        for (const auto& moon_index : moon_indices) {
            if (moon_index == body_index1) planetary_index1 = parent_index;
            if (moon_index == body_index2) planetary_index2 = parent_index;
        }
    }

    return planetary_index1.has_value() && planetary_index2.has_value() && *planetary_index1 == *planetary_index2;
}

void force_pairs::set_interaction_set(const std::array<PlanetarySystem, 7>& planetary_systems) {
    active_pairs.clear();

    switch (runtime_config::interaction_set) {
        case runtime_config::InteractionSet::full:
            for (const auto& force_pair : all_pairs) {
                active_pairs.emplace_back(force_pair);
            }
            break;
        case runtime_config::InteractionSet::no_moon_moon:
            for (const auto& force_pair : all_pairs) {
                if (!is_moon(force_pair.first_index, planetary_systems) ||
                    !is_moon(force_pair.second_index, planetary_systems)) {
                    active_pairs.emplace_back(force_pair);
                }
            }
            break;
        case runtime_config::InteractionSet::same_system_moons:
            for (const auto& force_pair : all_pairs) {
                if (is_moon(force_pair.first_index, planetary_systems) &&
                    is_moon(force_pair.second_index, planetary_systems)) {
                    if (have_same_parent(force_pair.first_index, force_pair.second_index, planetary_systems)) {
                        active_pairs.emplace_back(force_pair);
                    }
                } else {
                    active_pairs.emplace_back(force_pair);
                }
            }
            break;
        case runtime_config::InteractionSet::local_moon_systems:
            for (const auto& force_pair : all_pairs) {
                if (is_sun(force_pair.first_index) || is_sun(force_pair.second_index)) {
                    active_pairs.emplace_back(force_pair);
                } else if (is_primary(force_pair.first_index, planetary_systems) && is_primary(force_pair.second_index, planetary_systems)) {
                    active_pairs.emplace_back(force_pair);
                } else if (belong_to_same_planetary_system(force_pair.first_index, force_pair.second_index, planetary_systems)) {
                    active_pairs.emplace_back(force_pair);
                }
            }
            break;
        case runtime_config::InteractionSet::parent_sun_only:
            for (const auto& force_pair : all_pairs) {
                if (is_sun(force_pair.first_index) || is_sun(force_pair.second_index)) {
                    active_pairs.emplace_back(force_pair);
                } else if (is_primary(force_pair.first_index, planetary_systems) && is_primary(force_pair.second_index, planetary_systems)) {
                    active_pairs.emplace_back(force_pair);
                } else if (is_parent_moon_pair(force_pair.first_index, force_pair.second_index, planetary_systems)) {
                    active_pairs.emplace_back(force_pair);
                }
            }
            break;
    }
}

std::size_t force_pairs::active_pair_count() {
    return active_pairs.size();
}

std::size_t force_pairs::skipped_pair_count() {
    return all_pairs.size() - active_pairs.size();
}

void force_pairs::record_acceleration_evaluation() {
    recorded_accelerations++;
}

std::size_t force_pairs::acceleration_evaluation_count() {
    return recorded_accelerations;
}

std::size_t force_pairs::pair_evaluation_count() {
    return recorded_accelerations * active_pair_count();
}
