#pragma once
#include <stop_token>

#include "celestial_body.hpp"
#include "config.hpp"

namespace simulation {
    extern CelestialBody celestial_bodies[NUM_CELESTIAL_BODIES];
    extern std::array<std::deque<Vec3>, NUM_CELESTIAL_BODIES> orbit_history;

    void save_orbit_points();

    std::array<Vec3, NUM_CELESTIAL_BODIES> compute_accelerations();

    void simulate_step();

    // Builds a render snapshot from the simulation-owned state (no lock needed: the simulation thread is the only owner
    // of celestial_bodies & orbit_history) and publishes it atomically
    void publish_snapshot();

    void simulate_cpu(const std::stop_token& stop_token);

    void print_final_state();
}
