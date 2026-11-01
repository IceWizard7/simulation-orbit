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
#include "ui.hpp"

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

struct SimulationCheckpoint {
    std::size_t id = 0;
    str label;

    // Per body
    alignas(16) double px[NUM_CELESTIAL_BODIES];
    alignas(16) double py[NUM_CELESTIAL_BODIES];
    alignas(16) double pz[NUM_CELESTIAL_BODIES];

    alignas(16) double vx[NUM_CELESTIAL_BODIES];
    alignas(16) double vy[NUM_CELESTIAL_BODIES];
    alignas(16) double vz[NUM_CELESTIAL_BODIES];

    bool enabled[NUM_CELESTIAL_BODIES];
    double gravitational_masses[NUM_CELESTIAL_BODIES];

    // Simulation
    std::size_t steps_simulated;
    int time_step;

    // Model
    runtime_config::BodySet body_set;
    runtime_config::InteractionSet interaction_set;
    bool use_j2;
    bool use_simd;
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

    inline std::size_t next_checkpoint_id = 0;
    inline std::vector<SimulationCheckpoint> checkpoints;

    inline ui_data::DiagnosticsBaseline diagnostics_baseline;

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

    // Checkpoints
    void save_checkpoint();

    bool delete_checkpoint(std::size_t id);

    bool load_checkpoint(std::size_t id);

    bool rename_checkpoint(std::size_t id, const str& new_label);

    // Physical metrics (diagnostics)
    Vec3 calculate_linear_momentum();

    double calculate_total_energy();

    double calculate_total_mass();

    Vec3 calculate_barycenter(double total_mass);

    Vec3 calculate_expected_barycenter(const Vec3 &barycenter, const Vec3 &initial_linear_momentum, double total_mass, std::size_t baseline_steps_simulated);

    void reset_diagnostics_baseline();

    // Simulation
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
