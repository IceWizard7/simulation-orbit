#pragma once
#include <stop_token>

#include "celestial_body.hpp"
#include "config.hpp"

struct CSVEntry {
    size_t step{};
    std::array<Vec3, NUM_CELESTIAL_BODIES> positions{};
    std::array<Vec3, NUM_CELESTIAL_BODIES> velocities{};
};

namespace simulation {
    extern CelestialBody celestial_bodies[NUM_CELESTIAL_BODIES];
    extern std::vector<CSVEntry> csv_data;
    extern std::array<std::deque<Vec3>, NUM_CELESTIAL_BODIES> orbit_history;

    void save_orbit_points();

    void update_csv_data();

    std::array<Vec3, NUM_CELESTIAL_BODIES> compute_accelerations();

    void simulate_step();

    // Builds a render snapshot from the simulation-owned state (no lock needed: the simulation thread is the only owner
    // of celestial_bodies & orbit_history) and publishes it atomically
    void publish_snapshot();

    void simulate_cpu(const std::stop_token& stop_token);

    void write_csv_data();

    void print_final_state();
}
