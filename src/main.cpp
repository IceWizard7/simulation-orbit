#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
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
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "config.hpp"
#include "screen_utils.hpp"
#include "timer.hpp"
#include "vectors.hpp"

// TODO: Parallel Execution?
// TODO: 3d
// TODO: Real 3d textures
// TODO: Calculate how much "error" there is compared to real NASA data
// TODO: (=>) Analyze which forces we can skip calculating (or calculate ex. every 1000 steps) to reach certain accuracy
// TODO: Topic: ""
// TODO: Research question: "?"
// TODO: Add moons (=> how many?)
// TODO: On hover over a planet, display it's name + info (and maybe on a click display even more info?)
// TODO: Create 1 big namespace / class "solar system" or sth => separate stuff even more into different files

#define NUM_CELESTIAL_BODIES 10

str to_power_of10(const double x) {
    if (x == 0) return "0";

    const int exponent = static_cast<int>(std::floor(std::log10(std::fabs(x))));
    const double mantissa = x / std::pow(10.0, exponent);
    const str mantissaStr =
        std::fabs(mantissa - std::round(mantissa)) < 1e-9
            ? std::to_string(static_cast<long long>(std::round(mantissa)))
            : std::to_string(mantissa);

    if (std::fabs(mantissa - 1.0) < 1e-7) return "10^" + std::to_string(exponent);

    return mantissaStr + " x 10^" + std::to_string(exponent);
}

str round_to_hundreds(const double x) {
    str result = std::format("{:.2f}", x);

    if (const auto dot = result.find('.'); dot == str::npos) {
        return result;
    }

    // Pop extra zeros at the end (numbers without a dot don't reach this point)
    while (!result.empty() && result.back() == '0') {
        result.pop_back();
    }

    // Pop extra dots at the end
    if (!result.empty() && result.back() == '.') {
        result.pop_back();
    }

    // Avoid displaying "-0"
    if (result == "-0") {
        return "0";
    }

    return result;
}

constexpr int TARGET_FPS = 60;

std::atomic<std::size_t> steps_simulated = 0;
std::atomic center_celestial_body_changed = false;

Font uiFont;

PausableTimer timer;

class CelestialBody {
public:
    str name;
    Vec3 position;
    Vec3 velocity;
    double mass;
    float draw_radius;
    std::optional<Color> color;
    int max_rendered_orbit_segments_per_body;
    int max_rendered_orbit_tail;

    CelestialBody(str name, const Vec3 &position, const Vec3 &velocity, const double mass, const float radius, const std::optional<Color>& color, const int max_rendered_orbit_segments_per_body, const int max_rendered_orbit_tail) : name(std::move(name)), position(position), velocity(velocity), mass(mass), draw_radius(radius), color(color), max_rendered_orbit_segments_per_body(max_rendered_orbit_segments_per_body), max_rendered_orbit_tail(max_rendered_orbit_tail) {}

    [[nodiscard]] double distance_to(const CelestialBody &body) const {
        return (position - body.position).length();
    }

    [[nodiscard]] Vec3 acceleration_due_to(const CelestialBody &source) const {
        const Vec3 offset = position - source.position;
        const double distance = distance_to(source);

        // -(G * M / r^3) * offset
        return offset * (-config::GRAVITATIONAL_CONSTANT * source.mass
            / (distance * distance * distance));
    }
};

CelestialBody mercury = {"Mercury", {-3.229439434041441e10, -6.212384097453145e10, -2.058972617997836e9}, {3.337844168997828e4, -2.019057755964253e4, -4.710888632749314e3}, 3.302e23, 5, (Color){150, 150, 150, 255}, 40'000, 1'000};
CelestialBody venus = {"Venus", {-1.019802701272269e11, -3.651886112603541e10, 5.393515277422819e9}, {1.137949480576350e4, -3.319873100002852e4, -1.112329309713790e3}, 48.685e23, 5, (Color){245, 190,  70, 255}, 30'000, 10'000};
CelestialBody earth = {"Earth", {1.051110894240638e10, -1.524721760044149e11, 2.551204317737371e7}, {2.923284130475473e4, 2.012304925857257e3, -2.997543950911119e-1}, 5.97219e24, 5, (Color){ 40, 120, 204, 255}, 20'000, 10'000};
CelestialBody mars = {"Mars", {1.804158291514496e11, 1.155212196101544e11, -1.976959322734013e9}, {-1.217743703266605e4, 2.244555151648691e4, 7.689594977117213e2}, 6.4171e23, 10, (Color){220,  60,  40, 255}, 15'000, 10'000};
CelestialBody jupiter = {"Jupiter", {-4.339909949222860e11, 6.583216063749719e11, 6.981808430314541e9}, {-1.106477669156617e4, -6.573232170198393e3, 2.749669443388072e2}, 18.9819e26, 10, (Color){220, 150,  85, 255}, 4'000, 10'000};
CelestialBody saturn = {"Saturn", {1.402238236376539e12, 1.837816450614337e11, -5.902702510104157e10}, {-1.786665911022244e3, 9.555444950590967e3, -9.444982300526794e1}, 5.6834e26, 10, (Color){235, 205, 120, 255}, 4'000, 50'000};
CelestialBody uranus = {"Uranus", {1.386689779015193e12, 2.558518371479970e12, -8.462715242429852e9}, {-6.037314354491155e3, 2.927562363958545e3, 8.916577001311432e1}, 86.813e24, 10, (Color){ 80, 220, 220, 255}, 4'000, 50'000};
CelestialBody neptune = {"Neptune", {4.465613570420511e12, 1.599153765150425e11, -1.062078323030064e11}, {-2.302777319654876e2, 5.463337689178736e3, -1.078268539063705e2}, 102.409e24, 10, (Color){ 40,  80, 230, 255}, 4'000, 50'000};
CelestialBody pluto = {"Pluto", {2.947351321346399e12, -4.409737094120408e12, -3.806824198825967e11}, {4.656489570352034e3, 1.788853980502755e3, -1.545806860243079e3}, 1.307e22, 10, (Color){185, 155, 130, 255}, 4'000, 100'000};
CelestialBody sun   = {"Sun", {0, 0, 0}, {0, 0, 0}, 1.98847e30, 10, (Color){255, 230,  40, 255}, 30'000, 100'000};

int center_celestial_body_index = 0; // 0 -> sun; 3 -> earth

// Don't change order of celestial_bodies
CelestialBody* celestial_bodies[NUM_CELESTIAL_BODIES] = {&sun, &mercury, &venus, &earth, &mars, &jupiter, &saturn, &uranus, &neptune, &pluto};
std::array<std::deque<Vec3>, NUM_CELESTIAL_BODIES> orbit_history;

// Immutable, render-ready view of the system
// Producer: simulation thread, Consumer: render thread
// Positions & orbit points: relative to the center body => render thread can re-project them under the current zoom every frame
struct RenderSnapshot {
    struct Body {
        Vec3 position; float radius = 0; std::optional<Color> color;
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

    // TODO: Apparently it's more stable to update velocity before updating the position? Why?

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
        snap->bodies[i] = {pos, body.draw_radius, body.color};

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
    std::size_t since_check = 0;

    while (!stop_token.stop_requested()) {
        if (timer.is_running()) {
            simulate_step();

            // check the clock only every few thousand steps so steady_clock::now() doesn't dominate the loop
            since_check++;
            if (since_check >= 4096) {
                since_check = 0;
                const auto now = std::chrono::steady_clock::now();

                // publish a snapshot at twice the refresh rate
                if (now - last_publish >= std::chrono::milliseconds(1000 / (TARGET_FPS * 2))) {
                    publish_snapshot();
                    last_publish = now;
                }
            }

            if constexpr (config::TARGET_SIMULATION_SPEED > 0) {
                const double time_simulated = (config::TIME_STEP) * steps_simulated;
                const double real_time = timer.seconds();
                std::this_thread::sleep_for(std::chrono::milliseconds());
            }
        } else {
            if (center_celestial_body_changed) {
                publish_snapshot();
                center_celestial_body_changed = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / TARGET_FPS));
        }
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

void draw_ui(const std::shared_ptr<const RenderSnapshot>& snap) {
    constexpr int horizontal_lines = (config::WINDOW_HEIGHT - 2 * config::WINDOW_MARGIN) / config::GRID_SPACING + 1;
    constexpr int vertical_lines = (config::WINDOW_WIDTH - 2 * config::WINDOW_MARGIN) / config::GRID_SPACING + 1;

    const str SCALING_STRING = to_power_of10(config::SCALING / config::AXIS_SCALING);

    // Grid, axis & labels
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

    // Left side
    DrawText(uiFont, std::format("Simulation time: {} years", static_cast<int>(static_cast<double>(steps_simulated) * config::TIME_STEP / (86'400 * 365))).c_str(), Vec2(config::WINDOW_MARGIN, 10), 20, 1, BLACK);
    DrawText(uiFont, std::format("Computation time: {} seconds", round_to_hundreds(timer.seconds())).c_str(), Vec2(config::WINDOW_MARGIN, 30), 20, 1, BLACK);
    DrawText(uiFont, std::format("Step size: {}", config::TIME_STEP_STRING).c_str(), Vec2(config::WINDOW_MARGIN, 50), 20, 1, BLACK);

    // Right side
    DrawText(uiFont, std::format("Simulated years per second: {}", round_to_hundreds(std::ceil(((static_cast<double>(steps_simulated) * config::TIME_STEP / (86'400 * 365))) / timer.seconds()))).c_str(), Vec2(config::WINDOW_MARGIN + 400, 10), 20, 1, BLACK);
    DrawText(uiFont, std::format("Rendering relative to: {}", celestial_bodies[center_celestial_body_index]->name).c_str(), Vec2(config::WINDOW_MARGIN + 400, 30), 20, 1, BLACK);
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
            DrawCircle({config::WINDOW_WIDTH - config::WINDOW_MARGIN - 83, config::WINDOW_MARGIN + spacing * i + (font_size / 2)}, 5, *celestial_body->color);
            DrawText(uiFont, celestial_body->name.c_str(), Vec2(config::WINDOW_WIDTH - config::WINDOW_MARGIN - 75, config::WINDOW_MARGIN + spacing * i), font_size, 1, BLACK);
        }
    }

    DrawLine({config::WINDOW_WIDTH - config::GRID_SPACING - config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_WIDTH - config::GRID_SPACING - config::WINDOW_MARGIN, config::WINDOW_MARGIN + spacing * NUM_CELESTIAL_BODIES}, 2, BLACK);
    DrawLine({config::WINDOW_WIDTH - config::GRID_SPACING - config::WINDOW_MARGIN, config::WINDOW_MARGIN + spacing * NUM_CELESTIAL_BODIES}, {config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN + spacing * NUM_CELESTIAL_BODIES}, 2, BLACK);
}

void draw_planets(const std::shared_ptr<const RenderSnapshot>& snap) {
    // snap->bodies positions are already relative to the center body (see publish_snapshot)
    for (const auto& [position, radius, color] : snap->bodies) {
        if (color.has_value() && inside_screen(position)) {
            DrawCircleV(to_raylib(position), radius, *color);
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

void UpdateDrawFrame() {
    /**
    Keybinds

    General
        r: Reset scaling
        Space: Continue / Pause simulation

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

    **/

    const float scroll = GetMouseWheelMove();
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

    if (new_center_i != -1 && new_center_i != center_celestial_body_index) {
        center_celestial_body_index = new_center_i;
        center_celestial_body_changed = true;
    }

    if (IsKeyPressed(KEY_R)) {
        config::SCALING = config::ORIGINAL_SCALING;
        config::AXIS_SCALING = config::ORIGINAL_AXIS_SCALING;
    }

    if (IsKeyPressed(KEY_SPACE)) {
        timer.resume_or_pause();
    }

    std::shared_ptr<const RenderSnapshot> snap;
    {
        std::lock_guard lock(snapshot_lock);
        snap = latest_snapshot;
    }

    BeginDrawing();
    ClearBackground(WHITE);

    if (snap) {
        draw_orbits(snap);
        draw_planets(snap);
    }
    draw_legend();
    draw_ui(snap);

    EndDrawing();
}

int main() {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(config::WINDOW_WIDTH, config::WINDOW_HEIGHT, "Umlaufbahn Simulation");
    SetTargetFPS(TARGET_FPS);

    std::jthread cpu_thread(simulate_cpu);

    uiFont = LoadFontEx(
        "resources/JetBrainsMono-Regular.ttf",
        96,
        nullptr,
        0
    );

    GenTextureMipmaps(&uiFont.texture);
    SetTextureFilter(uiFont.texture, TEXTURE_FILTER_TRILINEAR);

    #ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
    #else
    while (!WindowShouldClose()) {
        UpdateDrawFrame();
    }
    #endif


    timer.pause();
    cpu_thread.request_stop();

    UnloadFont(uiFont);
    CloseWindow();
    printf("\n");
    printf("Simulation time: %.2f years\n", (static_cast<double>(steps_simulated) * config::TIME_STEP / (86'400 * 365)));
    printf("Computation time: %.2f seconds\n", timer.seconds());
    printf("Simulated years per second: %.2f\n", (static_cast<double>(steps_simulated) * config::TIME_STEP / (86'400 * 365)) / timer.seconds());

    return 0;
}
