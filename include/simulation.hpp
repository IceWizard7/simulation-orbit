#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <optional>
#include <raylib.h>
#include <span>
#include <stop_token>
#include <vector>

#include "celestial_bodies.hpp"
#include "config.hpp"
#include "graph_history.hpp"
#include "ui.hpp"

struct CSVEntry {
    step_t simulated_seconds{};
    step_t step{};
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

// Rendering-only trail sample
struct OrbitSample {
    step_t step;
    Vec3 position;
};

struct SimulationCheckpoint {
    std::uint64_t id = 0;
    str label;

    // Per body
    alignas(16) double px[NUM_CELESTIAL_BODIES];
    alignas(16) double py[NUM_CELESTIAL_BODIES];
    alignas(16) double pz[NUM_CELESTIAL_BODIES];

    alignas(16) double vx[NUM_CELESTIAL_BODIES];
    alignas(16) double vy[NUM_CELESTIAL_BODIES];
    alignas(16) double vz[NUM_CELESTIAL_BODIES];

    double physical_radius_m[NUM_CELESTIAL_BODIES];
    bool enabled[NUM_CELESTIAL_BODIES];

    double gravitational_masses[NUM_CELESTIAL_BODIES];
    double original_gravitational_masses[NUM_CELESTIAL_BODIES];
    str names[NUM_CELESTIAL_BODIES];

    float draw_radii_2d[NUM_CELESTIAL_BODIES];
    float draw_radii_3d[NUM_CELESTIAL_BODIES];
    Color colors[NUM_CELESTIAL_BODIES];

    std::optional<std::size_t> texture_ids[NUM_CELESTIAL_BODIES];

    double j2[NUM_CELESTIAL_BODIES];
    double equatorial_radii[NUM_CELESTIAL_BODIES];
    Vec3 pole_axes[NUM_CELESTIAL_BODIES];

    std::uint64_t body_ids[NUM_CELESTIAL_BODIES];

    // Simulation
    step_t steps_simulated;
    int time_step;
    double run_time_seconds = 0.0;

    // Model
    runtime_config::BodySet body_set;
    runtime_config::InteractionSet interaction_set;
    bool use_j2;
    bool use_simd;
};

namespace simulation {
    extern CelestialBodies<NUM_CELESTIAL_BODIES, NUM_CATALOG_BODIES> celestial_bodies;
    extern const std::array<PlanetarySystem, 7> planetary_systems;
    inline std::vector<CSVEntry> csv_data;

    // Rendering-only trail histories with explicit steps
    // (primary samples are adaptive, so the step can't be reconstructed from the index)
    // see the ORBIT_* constants in config.hpp
    inline std::array<std::deque<OrbitSample>, NUM_CELESTIAL_BODIES> orbit_history;
    inline std::array<std::deque<OrbitSample>, NUM_CELESTIAL_BODIES> orbit_center_reference_history;
    inline std::optional<Accelerations> accelerations_at_current_positions;

    // Primary sampling: velocity-direction turn (rad) since each body's last sample, measured relative to its
    // dynamic host from the latest hierarchy (stale by at most one publication, which is fine for sampling)
    inline std::array<double, NUM_CELESTIAL_BODIES> orbit_turn_since_sample{};
    inline std::array<std::optional<std::size_t>, NUM_CELESTIAL_BODIES> orbit_sampling_host{};
    inline step_t orbit_max_gap_steps = 1;

    // Coarse center-reference sampling (fixed cadence)
    inline step_t orbit_center_reference_sample_every_steps = 1;
    inline std::size_t orbit_center_reference_max_points = 2;

    inline std::uint64_t next_checkpoint_id = 0;
    inline std::vector<SimulationCheckpoint> checkpoints;

    inline ui_data::DiagnosticsBaseline diagnostics_baseline;

    inline GraphHistory graph_history;

    // Must run after CLI parsing (needs the final time step) and before the first simulate_step
    void initialize_orbit_sampling();

    // Replaces each planet and its modeled moons by one point mass at their GM-weighted barycenter
    // Must run after CLI parsing and before CSV/orbit initialization.
    void apply_planet_systems_approximation();

    void save_orbit_points();

    // Clears both trail histories and the sampling state (restore, physical edits)
    void clear_orbit_histories();

    void clear_orbit_history(std::size_t i); // One slot only

    std::optional<std::size_t> create_body(const ui_data::CreateBodyCommand& cmd); // Returns slot index or nullopt

    bool delete_body(std::size_t i);

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

    bool delete_checkpoint(std::uint64_t id);

    bool load_checkpoint(std::uint64_t id);

    // Builds a checkpoint from an imported state (catalog defaults from checkpoint 0) and restores it
    // Returns the new checkpoint's ID, or nullopt (SIMD / pruned interaction set / no checkpoint 0)
    std::optional<std::uint64_t> import_state(const ui_data::ImportStateCommand& cmd);

    bool rename_checkpoint(std::uint64_t id, const str& new_label);

    // Physical metrics (diagnostics)
    Vec3 calculate_linear_momentum();

    double calculate_kinetic_energy();

    double calculate_total_energy();

    double calculate_total_mass();

    Vec3 calculate_barycenter(double total_mass);

    Vec3 calculate_expected_barycenter(const Vec3& baseline_barycenter, const Vec3& initial_linear_momentum,
                                       double total_mass, step_t baseline_steps_simulated);

    void reset_diagnostics_baseline();

    // Graphs

    GraphSample make_graph_sample();

    // Simulation
    void simulate_cpu(const std::stop_token& stop_token);

    void print_final_state();

    // CSV
    extern std::ofstream live_csv_file;
    extern std::optional<step_t> last_csv_sample_step;
    extern bool csv_output_initialized;
    extern bool live_csv_write_failed;

    CSVEntry capture_csv_entry();

    void write_csv_header(std::ostream& output);

    void write_csv_entry(std::ostream& output, const CSVEntry& entry);

    bool report_live_csv_write_failure(step_t step);

    bool record_csv_sample(bool force);
}
