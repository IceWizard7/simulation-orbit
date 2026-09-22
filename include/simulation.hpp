#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <iosfwd>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

#include "celestial_bodies.hpp"
#include "config.hpp"

struct CSVEntry {
    std::size_t simulated_seconds{};
    std::size_t step{};
    std::array<std::optional<Vec3>, NUM_CELESTIAL_BODIES> positions{};
    std::array<std::optional<Vec3>, NUM_CELESTIAL_BODIES> velocities{};
};

struct PlanetarySystem {
    std::size_t parent_index;
    std::span<const std::size_t> moon_indices;
};

struct Accelerations {
    alignas(16) double x[NUM_CELESTIAL_BODIES];
    alignas(16) double y[NUM_CELESTIAL_BODIES];
    alignas(16) double z[NUM_CELESTIAL_BODIES];
};

namespace simulation {
    extern CelestialBodies<NUM_CELESTIAL_BODIES> celestial_bodies;
    extern const std::array<PlanetarySystem, 7> planetary_systems;
    inline std::vector<CSVEntry> csv_data;
    inline std::array<std::deque<Vec3>, NUM_CELESTIAL_BODIES> orbit_history;
    inline std::array<std::deque<Vec3>, NUM_CELESTIAL_BODIES> orbit_center_reference_history;
    inline std::optional<Accelerations> accelerations_at_current_positions;

    // Per-body primary trail sampling plus the common coarse center-reference sampling
    // (rendering-only; see the ORBIT_* constants in config.hpp)
    inline std::array<std::size_t, NUM_CELESTIAL_BODIES> orbit_sample_every_steps;
    inline std::array<std::size_t, NUM_CELESTIAL_BODIES> orbit_max_points;
    inline std::size_t orbit_center_reference_sample_every_steps;
    inline std::size_t orbit_center_reference_max_points;

    // Must run after CLI parsing (needs the final time step) and before the first simulate_step
    void initialize_orbit_sampling();

    // Replaces each planet and its modeled moons by one point mass at their GM-weighted barycenter
    // Must run after CLI parsing and before CSV/orbit initialization.
    void apply_planet_systems_approximation();

    void save_orbit_points();

    bool initialize_csv_output();

    void update_csv_data();

    bool finalize_csv_output();

    Accelerations compute_accelerations();

    void simulate_step();

    // Builds a render snapshot from the simulation-owned state (no lock needed: the simulation thread is the only owner
    // of celestial_bodies and both orbit histories) and publishes it atomically
    void publish_snapshot();

    void simulate_cpu(const std::stop_token& stop_token);

    void print_final_state();

    // CSV
    extern std::ofstream live_csv_file;
    extern std::optional<std::size_t> last_csv_sample_step;
    extern bool csv_output_initialized;
    extern bool live_csv_write_failed;

    CSVEntry capture_csv_entry();

    void write_csv_header(std::ostream& output);

    void write_csv_entry(std::ostream& output, const CSVEntry& entry);

    bool report_live_csv_write_failure(std::size_t step);

    bool record_csv_sample(bool force);
}
