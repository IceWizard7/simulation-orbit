#pragma once
#include <stop_token>

#include "celestial_body.hpp"
#include "config.hpp"

struct CSVEntry {
    size_t step{};
    std::array<std::optional<Vec3>, NUM_CELESTIAL_BODIES> positions{};
    std::array<std::optional<Vec3>, NUM_CELESTIAL_BODIES> velocities{};
};

namespace simulation {
    extern CelestialBody celestial_bodies[NUM_CELESTIAL_BODIES];
    inline std::vector<CSVEntry> csv_data;
    inline std::array<std::deque<Vec3>, NUM_CELESTIAL_BODIES> orbit_history;
    inline std::optional<std::array<Vec3, NUM_CELESTIAL_BODIES>> accelerations_at_current_positions;

    // Per-body orbit-trail sampling derived from orbital_period_seconds and the runtime --dt
    // (rendering-only; see config::ORBIT_POINTS_PER_REVOLUTION)
    inline std::array<std::size_t, NUM_CELESTIAL_BODIES> orbit_sample_every_steps;
    inline std::array<std::size_t, NUM_CELESTIAL_BODIES> orbit_max_points;

    // Must run after CLI parsing (needs the final time step) and before the first simulate_step
    void initialize_orbit_sampling();

    void save_orbit_points();

    bool initialize_csv_output();

    void update_csv_data();

    bool finalize_csv_output();

    std::array<Vec3, NUM_CELESTIAL_BODIES> compute_accelerations();

    void simulate_step();

    // Builds a render snapshot from the simulation-owned state (no lock needed: the simulation thread is the only owner
    // of celestial_bodies & orbit_history) and publishes it atomically
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
