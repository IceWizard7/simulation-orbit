#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <raylib.h>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "raymath.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "config.hpp"
#include "render3d.hpp"
#include "screen_utils.hpp"
#include "timer.hpp"
#include "vectors.hpp"

// TODO: Parallel Execution?
// TODO: Real 3d textures
// TODO: Calculate how much "error" there is compared to real NASA data
// TODO: (=>) Analyze which forces we can skip calculating (or calculate ex. every 1000 steps) to reach certain accuracy
// TODO: Topic: ""
// TODO: Research question: "?"
// TODO: Add moons (=> how many?)
// TODO: On hover over a planet, display it's name + info (and maybe on a click display even more info?)

// TODO: Add a "copy" button for positions etc.?

#define NUM_CELESTIAL_BODIES 10

namespace solar_system {
    std::atomic<std::size_t> steps_simulated = 0;
    std::atomic center_celestial_body_changed = false;

    Font uiFont;
    PausableTimer timer;

    double simulated_years() {
        return static_cast<double>(steps_simulated.load()) * config::TIME_STEP / config::SECONDS_PER_YEAR;
    }

    double measured_simulation_speed() {
        const double seconds = timer.seconds();
        return seconds > 0.0 ? simulated_years() / seconds : 0.0;
    }

    class CelestialBody {
    public:
        str name;
        Vec3 position;
        Vec3 velocity;
        double mass;
        double gravitational_mass; // mass * G
        float draw_radius_2d;
        float draw_radius_3d;
        std::optional<Color> color;
        int max_rendered_orbit_segments_per_body;
        int max_rendered_orbit_tail;

        CelestialBody(str name, const Vec3 &position, const Vec3 &velocity, const double mass, const float radius_2d, const float radius_3d, const std::optional<Color>& color, const int max_rendered_orbit_segments_per_body, const int max_rendered_orbit_tail) : name(std::move(name)), position(position), velocity(velocity), mass(mass), gravitational_mass(mass * config::GRAVITATIONAL_CONSTANT), draw_radius_2d(radius_2d), draw_radius_3d(radius_3d), color(color), max_rendered_orbit_segments_per_body(max_rendered_orbit_segments_per_body), max_rendered_orbit_tail(max_rendered_orbit_tail) {}

        [[nodiscard]] double distance_to(const CelestialBody &body) const {
            return (position - body.position).length();
        }

        [[nodiscard]] Vec3 acceleration_due_to(const CelestialBody &source) const {
            const double distance = distance_to(source);
            const Vec3 direction = (source.position - position) / distance;

            // (G * M / r^2) * direction
            return direction * (source.gravitational_mass / (distance * distance));
        }
    };

    CelestialBody sun = {"Sun", {5.254258484891016e8, -8.691526594804802e8, -1.013312408460682e7}, {1.486418097392948e1, 1.867348026651063e0, -4.030379227978539e-1}, 1988410e24, 10, 0.03, (Color){255, 230,  40, 255}, 30'000, 100'000};
    CelestialBody mercury = {"Mercury", {-3.863295206424535e10, 3.095205136713375e10, 6.195274889256019e9}, {-4.061253084483177e4, -3.574724011520127e4, 8.385974494151380e2}, 3.302e23, 5, 0.01, (Color){150, 150, 150, 255}, 40'000, 1'000};
    CelestialBody venus = {"Venus", {-9.442641552608661e10, 4.918920270156867e10, 6.142101433096975e9}, {-1.646745669837079e4, -3.114773688845666e4, 5.450657096441862e2}, 48.685e23, 5, 0.01, (Color){245, 190,  70, 255}, 30'000, 10'000};
    CelestialBody earth = {"Earth", {-3.821000604658472e10, 1.410274684528763e11, 5.275940805160999e7}, {-2.920909465286638e4, -7.960531594616490e3, -6.079433916260868e0}, 5.97219e24, 5, 0.01, (Color){ 40, 120, 204, 255}, 20'000, 10'000};
    CelestialBody mars = {"Mars", {-1.603072902891413e11, -1.693943532268408e11, 4.939584574298635e8}, {1.848810083914610e4, -1.467137544186248e4, -7.687147050137604e2}, 6.4171e23, 10, 0.02, (Color){220,  60,  40, 255}, 15'000, 10'000};
    CelestialBody jupiter = {"Jupiter", {-6.174066213916292e9, 7.669935543317993e11, -2.919675836705565e9}, {-1.321414969549137e4, 4.963533982492354e2, 2.948696326167778e2}, 18.9819e26, 10, 0.15, (Color){220, 150,  85, 255}, 4'000, 10'000};
    CelestialBody saturn = {"Saturn", {-8.515017102896239e11, 1.061724056177945e12, 1.475763099241823e10}, {-8.067709138805704e3, -6.064681250795719e3, 4.267881978436692e2}, 5.6834e26, 10, 0.15, (Color){235, 205, 120, 255}, 4'000, 50'000};
    CelestialBody uranus = {"Uranus", {-2.732875467120085e12, 1.447663723744908e11, 3.619086410308249e10}, {-4.165944517727502e2, -7.115869467058308e3, -2.130718237900275e1}, 86.813e24, 10, 0.15, (Color){ 80, 220, 220, 255}, 4'000, 50'000};
    CelestialBody neptune = {"Neptune", {-3.037153665467541e12, -3.366573860211023e12, 1.392493943615987e11}, {4.002395710519806e3, -3.608893452530916e3, -1.753789063721323e1}, 102.409e24, 10, 0.15, (Color){ 40,  80, 230, 255}, 4'000, 50'000};
    CelestialBody pluto = {"Pluto", {5.435493283758588e12, -2.498597656155004e12, -1.304284834205698e12}, {2.629139130853487e3, 3.571607925259364e3, -1.120892268633253e3}, 1.307e22, 10, 0.15, (Color){185, 155, 130, 255}, 4'000, 100'000};

    int center_celestial_body_index = 0; // 0 -> sun; 3 -> earth
    int planet_info_display_index = -1; // -1 -> none

    // Don't change order of celestial_bodies
    CelestialBody* celestial_bodies[NUM_CELESTIAL_BODIES] = {&sun, &mercury, &venus, &earth, &mars, &jupiter, &saturn, &uranus, &neptune, &pluto};
    std::array<std::deque<Vec3>, NUM_CELESTIAL_BODIES> orbit_history;

    // Immutable, render-ready view of the system
    // Producer: simulation thread, Consumer: render thread
    // Positions & orbit points: relative to the center body => render thread can re-project them under the current zoom every frame
    struct RenderSnapshot {
        struct Body {
            str name; Vec3 position; float radius_2d = 0; float radius_3d = 0; std::optional<Color> color;
        };
        struct Orbit {
            std::vector<Vec3> points; std::optional<Color> color;
        };
        std::array<Body,  NUM_CELESTIAL_BODIES> bodies;
        std::array<Orbit, NUM_CELESTIAL_BODIES> orbits; // already decimated to max_rendered_orbit_segments_per_body
        std::size_t max_orbit_points_used = 0;
    };

    std::mutex snapshot_lock; // held only for the pointer swap (nanoseconds)
    std::shared_ptr<const RenderSnapshot> latest_snapshot; // produced by simulation thread, read by render thread

    void save_orbit_points() {
        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            if (steps_simulated % static_cast<int>(config::ORBIT_SAMPLE_EVERY_SECONDS / config::TIME_STEP) != 0) continue;

            auto& history = orbit_history[i];

            history.push_back(celestial_bodies[i]->position);

            if (history.size() > config::MAX_ORBIT_POINTS) {
                history.pop_front();
            }
        }
    }

    void simulate_step() {
        Vec3 accelerations[NUM_CELESTIAL_BODIES] = {};

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            for (int j = 0; j < NUM_CELESTIAL_BODIES; j++) {
                if (i == j) continue; // Do not apply gravity from this object to this
                accelerations[i] += celestial_bodies[i]->acceleration_due_to(*celestial_bodies[j]);
            }
        }

        // Semi-implicit (this is used) vs. forward Euler: update velocity before updating the positions

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            celestial_bodies[i]->velocity += accelerations[i] * config::TIME_STEP;
        }

        for (auto& celestial_body : celestial_bodies) {
            celestial_body->position += celestial_body->velocity * config::TIME_STEP;
        }

        ++steps_simulated;
        save_orbit_points();
    }

    // Builds a render snapshot from the simulation-owned state (no lock needed: the simulation thread is the only owner
    // of celestial_bodies & orbit_history) and publishes it atomically
    void publish_snapshot() {
        auto snap = std::make_shared<RenderSnapshot>();

        const Vec3 center = celestial_bodies[center_celestial_body_index]->position;
        const auto& center_history = orbit_history[center_celestial_body_index];

        std::size_t max_used = 0;

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            const auto& body = *celestial_bodies[i];

            Vec3 pos = body.position;
            if (config::RENDERING_COORDINATES_RELATIVE_TO_OBJECT) {
                pos -= center;
            }
            snap->bodies[i] = {body.name, pos, body.draw_radius_2d, body.draw_radius_3d, body.color};

            const auto& history = orbit_history[i];
            max_used = std::max(max_used, history.size());

            // Don't include center in snapshot
            if constexpr (config::RENDERING_COORDINATES_RELATIVE_TO_OBJECT) {
                if (i == center_celestial_body_index) continue;
            }
            if (history.size() < 2 || !body.color.has_value()) continue;

            snap->orbits[i].color = body.color;

            std::size_t stride = 1;
            const int new_history_size = std::min(static_cast<int>(history.size()), body.max_rendered_orbit_tail);
            if (new_history_size > body.max_rendered_orbit_segments_per_body + 1) {
                stride = new_history_size / body.max_rendered_orbit_segments_per_body;
            }

            snap->orbits[i].points.reserve(new_history_size / stride + 1);
            for (std::size_t j = 0; j < new_history_size; j += stride) {
                if (j < center_history.size()) {
                    snap->orbits[i].points.push_back(history[j] - center_history[j]);
                } else {
                    // Fallback: subtract current position of the planet and not historical positon
                    snap->orbits[i].points.push_back(history[j] - center);
                }
            }
        }

        snap->max_orbit_points_used = max_used;

        {
            std::lock_guard lock(snapshot_lock);
            latest_snapshot = std::move(snap);
        }
    }

    void simulate_cpu(const std::stop_token& stop_token) {
        auto last_publish = std::chrono::steady_clock::now();
        auto last_budget_update = std::chrono::steady_clock::now();
        double step_budget = 0.0;
        std::size_t since_check = 0;

        auto publish_if_due = [&] {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_publish >= std::chrono::milliseconds(1000 / (config::TARGET_FPS * 2))) {
                publish_snapshot();
                last_publish = now;
            }
        };

        while (!stop_token.stop_requested()) {
            constexpr std::size_t MAX_STEPS_PER_BATCH = 4096;
            constexpr double MAX_CATCH_UP_SECONDS = 0.25;
            if (!timer.is_running()) {
                step_budget = 0.0;
                last_budget_update = std::chrono::steady_clock::now();

                if (center_celestial_body_changed) {
                    publish_snapshot();
                    center_celestial_body_changed = false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1000 / config::TARGET_FPS));
                continue;
            }

            if constexpr (config::TARGET_TOTAL_SIMULATION_TIME > 0.0) {
                if (config::TARGET_TOTAL_SIMULATION_TIME <= simulated_years()) {
                    timer.pause();
                }
            }

            if constexpr (config::TARGET_SIMULATION_SPEED <= 0.0) {
                simulate_step();

                if (++since_check >= MAX_STEPS_PER_BATCH) {
                    since_check = 0;
                    publish_if_due();
                }

                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - last_budget_update).count();
            last_budget_update = now;

            constexpr double max_budget = std::max(
                1.0,
                config::TARGET_STEPS_PER_SECOND * MAX_CATCH_UP_SECONDS
            );

            step_budget = std::min(
                step_budget + elapsed * config::TARGET_STEPS_PER_SECOND,
                max_budget
            );

            const auto steps_to_run = static_cast<std::size_t>(
                std::min(step_budget, static_cast<double>(MAX_STEPS_PER_BATCH))
            );

            if (steps_to_run == 0) {
                const double seconds_to_next_step =
                    (1.0 - step_budget) / config::TARGET_STEPS_PER_SECOND;

                std::this_thread::sleep_for(std::chrono::duration<double>(
                    std::min(seconds_to_next_step, 1.0 / config::TARGET_FPS)
                ));
                continue;
            }

            for (std::size_t i = 0; i < steps_to_run; ++i) {
                simulate_step();
            }

            step_budget -= static_cast<double>(steps_to_run);
            publish_if_due();
        }
    }

    void DrawTextCenteredEx(const Font &font, const char *text, const Vec2 center, const float angle, const float fontSize, const float spacing, const Color color) {
        auto [x, y] = MeasureTextEx(font, text, fontSize, spacing);

        DrawTextPro(
            font,
            text,
            Vector2(static_cast<float>(center.x), static_cast<float>(center.y)),
            Vector2(x/2, y/2),
            angle,
            fontSize,
            spacing,
            color
        );
    }

    void DrawCircle(const Vec2& pos, const float radius, const Color color) {
        DrawCircleV(Vector2(static_cast<float>(pos.x), static_cast<float>(pos.y)), radius, color);
    }

    void DrawRectangle(const Vec2& a, const Vec2& b, const Color color) {
        const auto left = static_cast<float>(std::min(a.x, b.x));
        const auto top = static_cast<float>(std::min(a.y, b.y));
        const auto width = static_cast<float>(std::abs(b.x - a.x));
        const auto height = static_cast<float>(std::abs(b.y - a.y));

        DrawRectangleV({left, top}, {width, height}, color);
    }

    void DrawText(const Font &font, const char *text, const Vec2 &position, const float fontSize, const float spacing, const Color color) {
        DrawTextEx(font, text, Vector2(static_cast<float>(position.x), static_cast<float>(position.y)), fontSize, spacing, color);
    }

    void DrawLine(const Vec2& start_pos, const Vec2& end_pos, const float thick, const Color color) {
        DrawLineEx(Vector2(static_cast<float>(start_pos.x), static_cast<float>(start_pos.y)), Vector2(static_cast<float>(end_pos.x), static_cast<float>(end_pos.y)), thick, color);
    }

    void DrawTextOutlined(const Font& font, const char* text, const Vector2& pos, const float fontSize, const float spacing, const Color& fill, const Color& outline) {
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx || dy) {
                    DrawTextEx(font, text, {pos.x + static_cast<float>(dx), pos.y + static_cast<float>(dy)}, fontSize, spacing, outline);
                }
            }
        }
        DrawTextEx(font, text, pos, fontSize, spacing, fill); // colored fill on top
    }

    void draw_ui(const std::shared_ptr<const RenderSnapshot>& snap) {
        constexpr int horizontal_lines = (config::WINDOW_HEIGHT - 2 * config::WINDOW_MARGIN) / config::GRID_SPACING + 1;
        constexpr int vertical_lines = (config::WINDOW_WIDTH - 2 * config::WINDOW_MARGIN) / config::GRID_SPACING + 1;

        const str SCALING_STRING = to_power_of10(config::SCALING / config::AXIS_SCALING);

        // Grid, axes & labels
        for (int x = config::WINDOW_MARGIN; x <= config::WINDOW_WIDTH - config::WINDOW_MARGIN; x += static_cast<int>(config::GRID_SPACING)) {
            float thick = 1.5;
            Color color = Fade(DARKGRAY, 0.35f);
            if (x == config::WINDOW_MARGIN || x == config::WINDOW_WIDTH - config::WINDOW_MARGIN) {
                thick = 2;
                color = BLACK;
            }
            DrawLine(
                Vec2(x, config::WINDOW_MARGIN),
                Vec2(x, config::WINDOW_HEIGHT - config::WINDOW_MARGIN),
                thick,
                color
            );
            if (x != config::WINDOW_WIDTH - config::WINDOW_MARGIN && x != config::WINDOW_MARGIN) {
                DrawTextCenteredEx(
                    uiFont,
                    round_to_hundreds(((static_cast<double>(x) / config::GRID_SPACING) - static_cast<double>(vertical_lines) / 2 - 0.5) * config::AXIS_SCALING / static_cast<int>(vertical_lines / 2)).c_str(),
                    {static_cast<double>(x), static_cast<double>(config::WINDOW_HEIGHT - config::WINDOW_MARGIN + 25)},
                    315,
                    24,
                    1,
                    BLACK
                );
            }
        }

        for (int y = config::WINDOW_MARGIN; y <= config::WINDOW_HEIGHT - config::WINDOW_MARGIN; y += static_cast<int>(config::GRID_SPACING)) {
            float thick = 1.5;
            Color color = Fade(DARKGRAY, 0.35f);
            if (y == config::WINDOW_MARGIN || y == config::WINDOW_HEIGHT - config::WINDOW_MARGIN) {
                thick = 2;
                color = BLACK;
            }

            DrawLine(
                Vec2(config::WINDOW_MARGIN, y),
                Vec2(config::WINDOW_WIDTH - config::WINDOW_MARGIN, y),
                thick,
                color
            );
            if (y != config::WINDOW_WIDTH - config::WINDOW_MARGIN && y != config::WINDOW_MARGIN) {
                DrawTextCenteredEx(
                    uiFont,
                    round_to_hundreds(((static_cast<double>(y) / config::GRID_SPACING) - static_cast<double>(horizontal_lines) / 2 - 0.5) * config::AXIS_SCALING / static_cast<int>(horizontal_lines / 2)).c_str(),
                    {static_cast<double>(config::WINDOW_MARGIN - 25), static_cast<double>(y)},
                    315,
                    24,
                    1,
                    BLACK
                );
            }
        }

        DrawTextCenteredEx(
            uiFont,
            std::format("Y Position ({} m)", SCALING_STRING).c_str(),
            {static_cast<double>(config::WINDOW_MARGIN) / 2 - 15, static_cast<double>(config::WINDOW_HEIGHT) / 2},
            -90.0f,
            24,
            1,
            BLACK
        );

        DrawTextCenteredEx(
            uiFont,
            std::format("X Position ({} m)", SCALING_STRING).c_str(),
            {static_cast<double>(config::WINDOW_WIDTH) / 2, config::WINDOW_HEIGHT - static_cast<double>(config::WINDOW_MARGIN) / 2 + 15},
            0,
            24,
            1,
            BLACK
        );
    }

    void draw_legend() {
        // Legend

        constexpr double font_size = 16;
        constexpr double spacing = font_size + 2;
        const Vec2 start = {config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN};
        const Vec2 end = {config::WINDOW_WIDTH - config::GRID_SPACING - config::WINDOW_MARGIN, config::WINDOW_MARGIN + spacing * NUM_CELESTIAL_BODIES};

        DrawRectangle(start, end, WHITE);

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            if (const auto& celestial_body = celestial_bodies[i]; celestial_body->color.has_value()) {
                DrawCircle({config::WINDOW_WIDTH - config::WINDOW_MARGIN - 80, config::WINDOW_MARGIN + spacing * i + (font_size / 2)}, 5, *celestial_body->color);
                DrawText(uiFont, celestial_body->name.c_str(), Vec2(config::WINDOW_WIDTH - config::WINDOW_MARGIN - 65, config::WINDOW_MARGIN + spacing * i), font_size, 1, BLACK);
            }
        }

        DrawLine({config::WINDOW_WIDTH - config::GRID_SPACING - config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN}, 2, BLACK);
        DrawLine({config::WINDOW_WIDTH - config::GRID_SPACING - config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_WIDTH - config::GRID_SPACING - config::WINDOW_MARGIN, config::WINDOW_MARGIN + spacing * NUM_CELESTIAL_BODIES}, 2, BLACK);
        DrawLine({config::WINDOW_WIDTH - config::GRID_SPACING - config::WINDOW_MARGIN, config::WINDOW_MARGIN + spacing * NUM_CELESTIAL_BODIES}, {config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN + spacing * NUM_CELESTIAL_BODIES}, 2, BLACK);
        DrawLine({config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN + config::GRID_SPACING * 2}, 2, BLACK);
    }

    std::pair<Vec2, Vec2> center_button_coordinates() {
        return {{config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 10}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}};
    }

    void draw_planet_info() {
        if (planet_info_display_index == -1) return;

        CelestialBody* celestial_body = celestial_bodies[planet_info_display_index];

        DrawRectangle(config::WINDOW_MARGIN, config::WINDOW_MARGIN, 5.5 * config::GRID_SPACING, 1.5 * config::GRID_SPACING, WHITE);
        DrawLine({config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN}, 2, BLACK);
        DrawLine({config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 1.5 * config::GRID_SPACING}, 2, BLACK);
        DrawLine({config::WINDOW_MARGIN, config::WINDOW_MARGIN + 1.5 * config::GRID_SPACING}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 1.5 * config::GRID_SPACING}, 2, BLACK);
        DrawLine({config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_MARGIN, config::WINDOW_MARGIN + 1.5 * config::GRID_SPACING}, 2, BLACK);

        DrawText(uiFont, "Celestial body information", {config::WINDOW_MARGIN + 15, config::WINDOW_MARGIN + 10}, 20, 1, BLACK);

        DrawText(uiFont, "Use as center", {config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 10}, 20, 1, BLACK);

        DrawCircle({config::WINDOW_MARGIN + 22.5, config::WINDOW_MARGIN + 60}, 7.5, *celestial_body->color);
        if (1 <= planet_info_display_index && planet_info_display_index <= 9) {
            DrawText(uiFont, std::format("{} ({}{} planet from sun)", celestial_body->name, planet_info_display_index, get_numerical_suffix(planet_info_display_index)).c_str(), {config::WINDOW_MARGIN + 35, config::WINDOW_MARGIN + 50}, 20, 1, BLACK);
        } else {
            DrawText(uiFont, std::format("{} (index {})", celestial_body->name, planet_info_display_index).c_str(), {config::WINDOW_MARGIN + 35, config::WINDOW_MARGIN + 50}, 20, 1, BLACK);
        }
        DrawText(uiFont, std::format("Position: {:<34}m", celestial_body->position.to_string()).c_str(), {config::WINDOW_MARGIN + 15, config::WINDOW_MARGIN + 70}, 20, 1, BLACK);
        DrawText(uiFont, std::format("Velocity: {:<34}m/s", celestial_body->velocity.to_string()).c_str(), {config::WINDOW_MARGIN + 15, config::WINDOW_MARGIN + 90}, 20, 1, BLACK);
        DrawText(uiFont, std::format("Mass: {:<38}kg", celestial_body->mass).c_str(), {config::WINDOW_MARGIN + 15, config::WINDOW_MARGIN + 110}, 20, 1, BLACK);
    }


    void draw_stats() {
        const double seconds = timer.seconds();

        // Left side
        DrawText(uiFont, std::format("Simulation time: {} years", static_cast<int>(static_cast<double>(steps_simulated) * config::TIME_STEP / (86'400 * 365))).c_str(), Vec2(config::WINDOW_MARGIN, 10), 20, 1, BLACK);
        DrawText(uiFont, std::format("Computation time: {} seconds", round_to_hundreds(seconds)).c_str(), Vec2(config::WINDOW_MARGIN, 30), 20, 1, BLACK);
        DrawText(uiFont, std::format("Step size: {}", config::TIME_STEP_STRING).c_str(), Vec2(config::WINDOW_MARGIN, 50), 20, 1, BLACK);

        // Right side
        DrawText(uiFont, std::format("Simulated years per second: {}", (seconds > 0.0 ? round_to_hundreds(((static_cast<double>(steps_simulated) * config::TIME_STEP / (86'400 * 365))) / seconds) : "0")).c_str(), Vec2(config::WINDOW_MARGIN + 400, 10), 20, 1, BLACK);
        DrawText(uiFont, std::format("Rendering relative to: {}", celestial_bodies[center_celestial_body_index]->name).c_str(), Vec2(config::WINDOW_MARGIN + 400, 30), 20, 1, BLACK);
        DrawText(uiFont, std::format("Target years per second: {}", config::TARGET_SIMULATION_SPEED > 0.0 ? round_to_hundreds(config::TARGET_SIMULATION_SPEED) : "Unlimited").c_str(), Vec2(config::WINDOW_MARGIN + 400, 50), 20, 1, BLACK);
    }

    void draw_planets(const std::shared_ptr<const RenderSnapshot>& snap) {
        bool timer_running = timer.is_running();

        // snap->bodies positions are already relative to the center body (see publish_snapshot)
        for (const auto& [name, position, radius_2d, _radius_3d, color] : snap->bodies) {
            if (color.has_value() && inside_screen(position)) {
                const Vector2 pos = to_raylib(position);
                DrawCircleV(pos, radius_2d, *color);
                Vector2 text_pos = pos;
                text_pos.y -= 10;
                text_pos.x += 10;
                if (!timer_running) {
                    DrawTextOutlined(uiFont, name.c_str(), text_pos, 20, 1, *color, {0, 0, 0, 125});
                }
            }
        }
    }

    void draw_orbits(const std::shared_ptr<const RenderSnapshot>& snap) {
        for (const auto& [points, color] : snap->orbits) {
            if (!color.has_value() || points.size() < 2) continue;

            const auto segment_count = static_cast<float>(points.size() - 1);

            for (std::size_t j = 1; j < points.size(); j++) {
                const float age = static_cast<float>(j) / segment_count;
                const float alpha = 0.10f + 0.70f * age;

                const Vec3 start = points[j - 1];
                const Vec3 end = points[j];

                if (inside_screen(start) && inside_screen(end)) {
                    DrawLineEx(
                        to_raylib(start),
                        to_raylib(end),
                        1.5f,
                        Fade(*color, alpha)
                    );
                }
            }
        }
    }

    void draw_orbits_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

        for (const auto& [pts, color] : snap->orbits) {
            if (!color || pts.size() < 2) continue;
            const auto n = static_cast<float>(pts.size() - 1);

            for (size_t j = 1; j < pts.size(); j++) {
                const Vector3 start = to_world(pts[j - 1]);
                const Vector3 end = to_world(pts[j]);

                // GetWorldToScreen doesn't clip points behind the camera
                // it can project them to a wrong on-screen spot, so skip those segments
                if (Vector3DotProduct(Vector3Subtract(start, cam.position), forward) <= 0) continue;
                if (Vector3DotProduct(Vector3Subtract(end, cam.position), forward) <= 0) continue;

                const float alpha = 0.10f + 0.70f * (static_cast<float>(j) / n);
                DrawLineEx(GetWorldToScreen(start, cam), GetWorldToScreen(end, cam), 2.0f, Fade(*color, alpha));
            }
        }
    }

    void draw_planets_3d(const std::shared_ptr<const RenderSnapshot>& snap) {
        for (const auto& [name, pos, _radius_2d, radius_3d, color] : snap->bodies) {
            if (color) DrawSphereEx(to_world(pos), radius_3d, 12, 12, *color);
        }
    }

    void draw_planet_labels_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
        if (timer.is_running()) return; // draw labels only while paused

        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

        for (const auto& [name, pos, _radius_2d, _radius_3d, color] : snap->bodies) {
            if (!color.has_value()) continue;

            const Vector3 world = to_world(pos);
            // GetWorldToScreen doesn't clip points behind the camera
            // it can project them to a wrong on-screen spot, so skip those segments
            if (Vector3DotProduct(Vector3Subtract(world, cam.position), forward) <= 0) continue;

            Vector2 text_pos = GetWorldToScreen(world, cam);
            text_pos.y -= 10;
            text_pos.x += 10;
            DrawTextOutlined(uiFont, name.c_str(), text_pos, 20, 1, *color, {0, 0, 0, 125});
        }
    }

    void draw_axes_3d() {
        constexpr float L = 10.0f;

        // x
        DrawLine3D({-L, 0, 0}, {L, 0, 0}, RED);
        DrawTriangle3D(
            {0.90 * L, 0, 0.025 * L},
            {L, 0, 0},
            {0.90 * L, 0, 0.025 * -L},
            RED
        );

        // y
        DrawLine3D({0, -L, 0}, {0, L, 0}, GREEN);
        DrawTriangle3D(
            {0.025 * L, 0.90 * L, 0},
            {0, L, 0},
            {0.025 * -L, 0.90 * L, 0},
            GREEN
        );

        // z
        DrawLine3D({0, 0, -L}, {0, 0, L}, BLUE);
        DrawTriangle3D(
            {0.025 * L, 0, 0.90 * L},
            {0.025 * -L, 0, 0.90 * L},
            {0, 0, L},
            BLUE
        );
    }

    void draw_axes_labels_3d(const Camera3D& cam) {
        // TODO: Should change percentile when zooming in & out

        const auto forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        auto label_axis = [&](auto anchor_of, const char* unit) {
            for (int k = -8; k <= 8; k++) {
                if (k == 0) continue;
                const Vector3 w = anchor_of(k);

                // don't draw labels behind the camera
                if (Vector3DotProduct(Vector3Subtract(w, cam.position), forward) <= 0) continue;

                auto [x, y] = GetWorldToScreen(w, cam);
                str text = to_power_of10(k * config::WORLD_UNIT_METERS);
                DrawText(uiFont, text.c_str(), {x, y}, 14, 1, LIGHTGRAY);
            }
        };

        label_axis([](const int k){return Vector3{static_cast<float>(k), 0, 0};}, "x");
        label_axis([](const int k){return Vector3{0, 0, static_cast<float>(k)};}, "x"); // physics y -> raylib z
        label_axis([](const int k){return Vector3{0, static_cast<float>(k), 0};}, "x"); // raylib z -> raylib y
    }

    float cam_azimuth = config::DEFAULT_CAM_AZIMUTH; // 0
    float cam_elevation = config::DEFAULT_CAM_ELEVATION; // 35° shows 3D immediately
    float cam_distance = config::DEFAULT_CAM_DISTANCE; // radius in world units
    bool view_3d = false; // toggle with keybind

    int pick_body_at_mouse_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
        if (!snap) return -1;

        const auto [mx, my] = GetMousePosition();

        int best = -1;
        float best_dist_sq = 0;

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            constexpr float MIN_CLICK_RADIUS = 8.0f; // keeps tiny planets clickable

            const auto& body = snap->bodies[i];
            if (!body.color.has_value() || !inside_screen(body.position)) continue;

            const auto [sx, sy] = to_raylib(body.position);
            const float dx = mx - sx;
            const float dy = my - sy;
            const float dist_sq = dx * dx + dy * dy;

            const float hit_radius = std::max(body.radius_2d, MIN_CLICK_RADIUS);
            if (dist_sq <= hit_radius * hit_radius && (best == -1 || dist_sq < best_dist_sq)) {
                best = i;
                best_dist_sq = dist_sq;
            }
        }

        return best;
    }

    int pick_body_at_mouse_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
        if (!snap) return -1;

        const auto [mx, my]   = GetMousePosition();
        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

        int best = -1;
        float best_cam_dist = 0; // front-most body under the cursor wins

        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            constexpr float MIN_CLICK_RADIUS_PX = 12.0f;
            const auto& body = snap->bodies[i];
            if (!body.color.has_value()) continue;

            const Vector3 world = to_world(body.position);

            // Behind the camera → GetWorldToScreen would give a bogus spot, so skip
            if (Vector3DotProduct(Vector3Subtract(world, cam.position), forward) <= 0) continue;

            const auto [sx, sy] = GetWorldToScreen(world, cam);
            const float dx = mx - sx;
            const float dy = my - sy;

            const Vector3 edge = Vector3Add(world, Vector3Scale(right, body.radius_3d));
            const auto [ex, ey] = GetWorldToScreen(edge, cam);
            const float radius_px  = std::hypot(ex - sx, ey - sy); // sphere radius in pixels
            const float hit_radius = std::max(radius_px, MIN_CLICK_RADIUS_PX);

            if (dx * dx + dy * dy > hit_radius * hit_radius) continue;

            const float cam_dist = Vector3Distance(cam.position, world);
            if (best == -1 || cam_dist < best_cam_dist) {
                best = i;
                best_cam_dist = cam_dist;
            }
        }

        return best;
    }

    Camera3D make_camera() {
        Camera3D c{};
        c.target = {0, 0, 0};
        c.up = {0, 1, 0};
        c.fovy = 45.0f;
        c.projection = CAMERA_PERSPECTIVE; // alternative: CAMERA_ORTHOGRAPHIC
        c.position = {
            cam_distance * cosf(cam_elevation) * cosf(cam_azimuth),
            cam_distance * sinf(cam_elevation),
            cam_distance * cosf(cam_elevation) * sinf(cam_azimuth)
        };

        return c;
    }

    /*
    Keybinds

    General
        r: Reset scaling
        Space: Continue / Pause simulation
        t: Change 2d/3d
        Left click: More info on celestial body

    Zooming
        Scrolling: Zoom in & out
        +: Zoom in
        -: Zoom in

    Changing center celestial body
        0: Sun
        1: Mercury
        2: Venus
        3: Earth
        4: Mars
        5: Jupiter
        6: Saturn
        7: Uranus
        8: Neptun
        9: Pluto

    3D-only
        Right click: Change angle of camera
    */
    void UpdateDrawFrame() {
        std::shared_ptr<const RenderSnapshot> snap;
        {
            std::lock_guard lock(snapshot_lock);
            snap = latest_snapshot;
        }

        int new_center_i = -1;

        if (IsKeyPressed(KEY_ZERO)) { new_center_i=0; }
        if (IsKeyPressed(KEY_ONE)) { new_center_i=1; }
        if (IsKeyPressed(KEY_TWO)) { new_center_i=2; }
        if (IsKeyPressed(KEY_THREE)) { new_center_i=3; }
        if (IsKeyPressed(KEY_FOUR)) { new_center_i=4; }
        if (IsKeyPressed(KEY_FIVE)) { new_center_i=5; }
        if (IsKeyPressed(KEY_SIX)) { new_center_i=6; }
        if (IsKeyPressed(KEY_SEVEN)) { new_center_i=7; }
        if (IsKeyPressed(KEY_EIGHT)) { new_center_i=8; }
        if (IsKeyPressed(KEY_NINE)) { new_center_i=9; }

        bool center_button_pressed = false;

        if (planet_info_display_index != -1 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            auto [b_start, b_end] = center_button_coordinates();

            if (const auto [mx, my] = GetMousePosition();
                b_start.x <= mx && mx <= b_end.x &&
                b_start.y <= my && my <= b_end.y) {
                center_button_pressed = true;
                }
        }

        if (!center_button_pressed) {
            int selected_body_i = -2;
            if (view_3d) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    selected_body_i = pick_body_at_mouse_3d(snap, make_camera());
                }
            }
            else {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    selected_body_i = pick_body_at_mouse_2d(snap);
                }
            }

            if (selected_body_i != -2 && selected_body_i != planet_info_display_index) {
                planet_info_display_index = selected_body_i;
            }
        }

        if (center_button_pressed) {
            center_celestial_body_index = planet_info_display_index;
            center_celestial_body_changed = true;
            planet_info_display_index = -1; // optional: reset planet_info_display_index
        } else if (new_center_i != -1 && new_center_i != center_celestial_body_index) {
            center_celestial_body_index = new_center_i;
            center_celestial_body_changed = true;
        }

        if (IsKeyPressed(KEY_R)) {
            config::SCALING = config::ORIGINAL_SCALING;
            config::AXIS_SCALING = config::ORIGINAL_AXIS_SCALING;
        }

        if (IsKeyPressed(KEY_SPACE)) {
            if (timer.is_running()) {
                timer.pause();
            } else {
                if (config::TARGET_TOTAL_SIMULATION_TIME < 0.0 || config::TARGET_TOTAL_SIMULATION_TIME > simulated_years()) {
                    timer.resume();
                }
            }

        }

        if (IsKeyPressed(KEY_T)) view_3d = !view_3d;

        const float scroll = GetMouseWheelMove();

        if (view_3d) {
            // TODO: Make arrow keys work too!
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                auto [x, y] = GetMouseDelta();
                cam_azimuth   += x * config::DRAG_SENSITIVITY; // left/right = spin
                cam_elevation += y * config::DRAG_SENSITIVITY; // up/down = tilt
                constexpr float lim = 89.0f * DEG2RAD; // clamp
                cam_elevation = std::clamp(cam_elevation, -lim, lim);
            }
            const float zoom_factor = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? (config::ZOOM_3D_FACTOR*config::ZOOM_3D_FACTOR) : config::ZOOM_3D_FACTOR;

            if (scroll > 0 || IsKeyDown(KEY_RIGHT_BRACKET)) { // "+" on QWERTZ
                cam_distance *= 1/zoom_factor;
            } else if (scroll < 0 || IsKeyDown(KEY_SLASH)) { // "-" on QWERTZ
                cam_distance *= zoom_factor;
            }

            cam_distance = std::clamp(cam_distance, 0.1f, 200.0f);
            if (IsKeyPressed(KEY_R)) {
                // reset azimuth/elevation/distance to defaults
                cam_azimuth = config::DEFAULT_CAM_AZIMUTH;
                cam_elevation = config::DEFAULT_CAM_ELEVATION;
                cam_distance = config::DEFAULT_CAM_DISTANCE;
            }
        } else {
            const double zoom_factor = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? (config::ZOOM_FACTOR*config::ZOOM_FACTOR) : config::ZOOM_FACTOR;
            if (scroll > 0 || IsKeyDown(KEY_RIGHT_BRACKET)) { // "+" on QWERTZ
                // scrolled up (=> in)
                if (config::AXIS_SCALING / zoom_factor < 4.0/3) {
                    config::AXIS_SCALING *= 10;
                    config::AXIS_SCALING /= zoom_factor;
                    config::SCALING /= zoom_factor;
                } else {
                    config::AXIS_SCALING /= zoom_factor;
                    config::SCALING /= zoom_factor;
                }
            } else if (scroll < 0 || IsKeyDown(KEY_SLASH)) { // "-" on QWERTZ
                // scrolled down (=> out)
                if (config::AXIS_SCALING * zoom_factor > 10 * 4.0/3) {
                    config::AXIS_SCALING /= 10;
                    config::AXIS_SCALING *= zoom_factor;
                    config::SCALING *= zoom_factor;
                } else {
                    config::AXIS_SCALING *= zoom_factor;
                    config::SCALING *= zoom_factor;
                }
            }
        }

        BeginDrawing();
        ClearBackground(WHITE);

        if (view_3d) {
            const Camera3D cam = make_camera();

            BeginMode3D(cam);
            // DrawGrid(20, 1.0f);
            draw_axes_3d();
            if (snap) draw_planets_3d(snap);

            EndMode3D();

            // 2D overlays, projected by 3D points
            if (snap) {
                draw_orbits_3d(snap, cam);
                draw_planet_labels_3d(snap, cam);
            }

            // draw_axes_labels_3d(cam);
            DrawRectangle(0, 0, config::WINDOW_WIDTH, config::WINDOW_MARGIN, WHITE);
            // DrawLine({config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN}, 2, BLACK);
            draw_legend();
            draw_stats();
            draw_planet_info();
        } else {
            if (snap) {
                draw_orbits(snap);
                draw_planets(snap);
            }
            draw_legend();
            draw_stats();
            draw_ui(snap);
            draw_planet_info();
        }


        EndDrawing();
    }
}

int main() {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(config::WINDOW_WIDTH, config::WINDOW_HEIGHT, "Umlaufbahn Simulation");
    SetTargetFPS(config::TARGET_FPS);

    std::jthread cpu_thread(solar_system::simulate_cpu);

    solar_system::uiFont = LoadFontEx(
        "resources/JetBrainsMono-Regular.ttf",
        96,
        nullptr,
        0
    );

    GenTextureMipmaps(&solar_system::uiFont.texture);
    SetTextureFilter(solar_system::uiFont.texture, TEXTURE_FILTER_TRILINEAR);

    #ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
    #else
    while (!WindowShouldClose()) {
        solar_system::UpdateDrawFrame();
    }
    #endif


    solar_system::timer.pause();
    cpu_thread.request_stop();

    UnloadFont(solar_system::uiFont);
    CloseWindow();
    printf("\n");

    const double seconds = solar_system::timer.seconds();

    printf("Computation time: %.2f seconds\n", seconds);
    printf("Simulated years per second: %.2f\n\n", seconds > 0.0 ? (static_cast<double>(solar_system::steps_simulated) * config::TIME_STEP / (86'400 * 365)) / seconds : 0);

    printf("Simulation time: %.8f years (@ 365 days)\n", solar_system::simulated_years());
    printf("Steps simulated: %zu\n", solar_system::steps_simulated.load());
    printf("Time step: %.17g seconds\n", config::TIME_STEP);

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const auto& celestial_body = solar_system::celestial_bodies[i];
        printf("%s", celestial_body->position.to_exact_string().c_str());
        if (i != NUM_CELESTIAL_BODIES - 1) {
            printf("|");
        }
    }
    printf("\n");

    return 0;
}
