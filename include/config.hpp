#pragma once

#include <filesystem>
#include <fstream>
#include <raylib.h>

#include "celestial_body.hpp"
#include "timer.hpp"
#include "utils.hpp"

#define NUM_CELESTIAL_BODIES 38
#define NUM_PLANETS 9 // excluding dwarf planets & sun; always located at the first indices
#define NUM_DWARF_PLANETS 1 // only dwarf planets

namespace ui {
    struct RenderSnapshot;
}

namespace config {
    constexpr double ORIGINAL_SCALING = 8e12;
    constexpr double ORIGINAL_AXIS_SCALING = 8;

    constexpr double ZOOM_FACTOR = 1.0717734625362931; // n-th root of 10 works, because then ZOOM_FACTOR**n = 10 => near perfect zoom cycle

    // Orbit trails are sampled per body from its orbital_period_seconds: every body gets the same
    // angular resolution regardless of period
    // ORBIT_TAIL_REVOLUTIONS sets how many revolutions each trail keeps: ~1.05 -> one clean closed curve, higher -> faded precession spirals
    // Memory & per-frame cost scale with REVOLUTIONS * POINTS (points per body ~= that product)
    constexpr int ORBIT_POINTS_PER_REVOLUTION = 1'024;
    constexpr double ORBIT_TAIL_REVOLUTIONS = 10;

    // Coarse, long-lived positions used only to reconstruct a moving center for old, distant
    // orbit samples. 1.05 of Pluto's period keeps at least one complete outer-planet orbit
    // available without retaining every moon's high-frequency history for centuries
    constexpr int ORBIT_CENTER_REFERENCE_POINTS_PER_YEAR = 64;
    constexpr double ORBIT_CENTER_REFERENCE_LONGEST_REVOLUTIONS = 1.05;

    constexpr bool RENDERING_COORDINATES_RELATIVE_TO_OBJECT = true;

    constexpr double GRAVITATIONAL_CONSTANT = 6.6743e-11; // m^3 / (kg * s^2)

    constexpr int WINDOW_HEIGHT = 900;
    constexpr int WINDOW_WIDTH = 900;

    constexpr int GRID_SPACING = 90;
    constexpr int WINDOW_MARGIN = 1 * GRID_SPACING;

    inline double SCALING = ORIGINAL_SCALING;
    inline double AXIS_SCALING = ORIGINAL_AXIS_SCALING;

    constexpr double WORLD_UNIT_METERS = 1e12;

    constexpr float DEFAULT_CAM_AZIMUTH = 45.0f * DEG2RAD;
    constexpr float DEFAULT_CAM_ELEVATION = 35.0f * DEG2RAD;
    constexpr float DEFAULT_CAM_DISTANCE = 25.0f;
    constexpr bool DEFAULT_VIEW_3D = false;

    constexpr float DRAG_SENSITIVITY = 0.01;
    constexpr float ZOOM_3D_FACTOR = 1.0717734625362931;

    constexpr double SECONDS_PER_YEAR = 86'400 * 365;

    constexpr int TARGET_FPS = 60;

    // Shared
    inline std::mutex snapshot_lock; // held only for the pointer swap (nanoseconds)
    inline std::shared_ptr<const ui::RenderSnapshot> latest_snapshot; // produced by simulation thread, read by render thread

    inline PausableTimer timer;
    inline std::atomic<std::size_t> steps_simulated = 0;
    inline std::atomic republish_needed = false;

    inline Font uiFont;

    inline std::atomic center_celestial_body_index = 0; // 0 -> sun; 3 -> earth
    inline std::atomic planet_info_display_index = -1; // -1 -> none
}

namespace runtime_config {
    enum class BodySet {
        all,
        planets,
        dwarf,
        planet_systems,
    };

    extern str invocation_command;
    extern bool exit_immediately;
    extern bool headless;
    extern int time_step;
    extern double half_dt_squared;
    extern double half_dt;
    extern str time_step_string;

    extern double target_total_simulation_time; // years; <= 0 unlimited
    extern double target_simulation_speed; // years per second; <= 0 unlimited
    extern double target_steps_per_second;
    extern std::optional<std::size_t> target_steps;

    extern std::optional<std::filesystem::path> csv_path;
    extern std::optional<int> sample_csv_data_every_seconds;
    extern std::optional<std::size_t> sample_csv_data_every_steps;
    extern bool csv_live;

    extern bool use_j2;

    extern BodySet body_set;

    [[nodiscard]] const char* body_set_name();

    void update_body_set(CelestialBody (&celestial_bodies)[NUM_CELESTIAL_BODIES]);

    extern int enabled_celestial_bodies;

    template <typename T>
    struct ParseRes {
        T val;
        int err;
    };

    ParseRes<double> parse_double(const str& argument_name, int i, int argc, char* argv[]);

    ParseRes<int> parse_int(const str& argument_name, int i, int argc, char* argv[]);

    ParseRes<str> parse_str(const str& argument_name, int i, int argc, char* argv[]);

    int parse_cli_args(int argc, char* argv[], CelestialBody (&celestial_bodies)[NUM_CELESTIAL_BODIES]);

    void set_time_step_string();
};
