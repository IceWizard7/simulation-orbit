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

// TODO: Parallel Execution?
// TODO: 3d
// TODO: Real 3d textures
// TODO: Calculate how much "error" there is compared to real NASA data
// TODO: (=>) Analyze which forces we can skip calculating (or calculate ex. every 1000 steps) to reach certain accuracy
// TODO: Topic: ""
// TODO: Research question: "?"
// TODO: Add moons (=> how many?)

using str = std::string;

#define NUM_CELESTIAL_BODIES 10

str to_power_of10(const double x) {
    if (x == 0) return "0";

    const int exponent = static_cast<int>(std::floor(std::log10(std::fabs(x))));
    const double mantissa = x / std::pow(10.0, exponent);
    const str mantissaStr =
        std::fabs(mantissa - std::round(mantissa)) < 1e-9
            ? std::to_string(static_cast<long long>(std::round(mantissa)))
            : std::to_string(mantissa);

    if (std::fabs(mantissa - 1.0) < 1e-9) return "10^" + std::to_string(exponent);

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

constexpr double ORIGINAL_SCALING = 2e12;
constexpr double ORIGINAL_AXIS_SCALING = 2;
constexpr double ZOOM_FACTOR = 1.0717734625362931; // n-th root of 10 works, because then ZOOM_FACTOR**n = 10 => near perfect zoom cycle
constexpr double TIME_STEP = 86'400;
constexpr int TARGET_SIMULATION_SPEED = 10; // simulated seconds per second; -1 for infinite
constexpr str TIME_STEP_STRING = "24 hrs";
constexpr int MAX_ORBIT_POINTS = 100'000; // => ~22.9 MiB RAM for orbit_history
constexpr int ORBIT_SAMPLE_EVERY_SECONDS = 259'200;
constexpr bool RENDERING_COORDINATES_RELATIVE_TO_OBJECT = true;

constexpr double GRAVITATIONAL_CONSTANT = 6.6743e-11;

constexpr int WINDOW_HEIGHT = 900;
constexpr int WINDOW_WIDTH = 900;

constexpr int GRID_SPACING = 90;
constexpr int WINDOW_MARGIN = 1 * GRID_SPACING;

double SCALING = ORIGINAL_SCALING;
double AXIS_SCALING = ORIGINAL_AXIS_SCALING;

constexpr int TARGET_FPS = 60;

std::atomic<std::size_t> steps_simulated = 0;
std::atomic center_celestial_body_changed = false;

Font uiFont;

class PausableTimer {
    using Clock = std::chrono::steady_clock;

    Clock::time_point last_start = Clock::now();
    Clock::duration elapsed{};

public:
    std::atomic<bool> running = true;

    void pause() {
        if (!running) return;

        elapsed += Clock::now() - last_start;
        running = false;
    }

    void resume() {
        if (running) return;
        last_start = Clock::now();
        running = true;
    }

    void resume_or_pause() {
        if (running) pause();
        else resume();
    }

    double seconds() const {
        auto total = elapsed;

        if (running) total += Clock::now() - last_start;

        return std::chrono::duration<double>(total).count();
    }
};

PausableTimer timer;

class Vec2 {
public:
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    Vec2(const double x, const double y) : x(x), y(y) {}

    [[nodiscard]] Vector2 to_raylib() const {
        // x in interval [0, WINDOW_WIDTH]
        // y in interval [0, WINDOW_HEIGHT]

        return {
            WINDOW_MARGIN + static_cast<float>((x / SCALING) / 2.0 + 0.5) * (WINDOW_WIDTH - 2 * WINDOW_MARGIN),
            WINDOW_MARGIN + static_cast<float>((y / SCALING) / 2.0 + 0.5) * (WINDOW_HEIGHT - 2 * WINDOW_MARGIN)
        };
    }

    [[nodiscard]] bool within_region(const Vec2 &a, const Vec2 &b, const float margin) const {
        auto [rx, ry] = to_raylib();

        return std::min(a.x - margin, b.x - margin) <= rx && rx <= std::max(a.x + margin, b.x + margin) &&
           std::min(a.y - margin, b.y - margin) <= ry && ry <= std::max(a.y + margin, b.y + margin);
    }
};


class Vec3 {
public:
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3() = default;
    Vec3(const double x, const double y, const double z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3 &v) const {
        return {x + v.x, y + v.y, z + v.z};
    }

    Vec3 operator-(const Vec3 &v) const {
        return {x - v.x, y - v.y, z - v.z};
    }

    Vec3 operator*(const double step) const {
        return {x * step, y * step, z * step};
    }

    Vec3 operator/(const double step) const {
        return {x / step, y / step, z / step};
    }

    Vec3 &operator+=(const Vec3 & v) {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }

    Vec3 &operator-=(const Vec3 & v) {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }

    [[nodiscard]] double length() const {
        return sqrt(x * x + y * y + z * z);
    }

    [[nodiscard]] str to_string() const {
        return "[" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + "]";
    }

    [[nodiscard]] Vector2 to_raylib() const {
        // x in interval [0, WINDOW_WIDTH]
        // y in interval [0, WINDOW_HEIGHT]

        return {
            WINDOW_MARGIN + static_cast<float>((x / SCALING) / 2.0 + 0.5) * (WINDOW_WIDTH - 2 * WINDOW_MARGIN),
            WINDOW_MARGIN + static_cast<float>((y / SCALING) / 2.0 + 0.5) * (WINDOW_HEIGHT - 2 * WINDOW_MARGIN)
        };
    }

    static bool inside_screen(const Vec3 &vec3) {
        auto [x, y] = vec3.to_raylib();

        return (WINDOW_MARGIN <= x && x <= WINDOW_WIDTH - WINDOW_MARGIN
            && WINDOW_MARGIN <= y && y <= WINDOW_HEIGHT - WINDOW_MARGIN
        );
    }

    [[nodiscard]] Vec2 to_vec2() const {
        return {x, y};
    }
};

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
        return offset * (-GRAVITATIONAL_CONSTANT * source.mass
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
        if (steps_simulated % static_cast<int>(ORBIT_SAMPLE_EVERY_SECONDS / TIME_STEP) != 0) continue;

        auto& history = orbit_history[i];

        history.push_back(celestial_bodies[i]->position);

        if (history.size() > MAX_ORBIT_POINTS) {
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
        celestial_bodies[i]->velocity += accelerations[i] * TIME_STEP;
    }

    for (auto& celestial_body : celestial_bodies) {
        celestial_body->position += celestial_body->velocity * TIME_STEP;
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
        if (RENDERING_COORDINATES_RELATIVE_TO_OBJECT) {
            pos -= center;
        }
        snap->bodies[i] = {pos, body.draw_radius, body.color};

        const auto& history = orbit_history[i];
        max_used = std::max(max_used, history.size());

        // Don't include center in snapshot
        if (RENDERING_COORDINATES_RELATIVE_TO_OBJECT && i == center_celestial_body_index) continue;
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
        if (timer.running) {
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

            if constexpr (TARGET_SIMULATION_SPEED > 0) {
                const double time_simulated = (TIME_STEP) * steps_simulated;
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
    constexpr int horizontal_lines = (WINDOW_HEIGHT - 2 * WINDOW_MARGIN) / GRID_SPACING + 1;
    constexpr int vertical_lines = (WINDOW_WIDTH - 2 * WINDOW_MARGIN) / GRID_SPACING + 1;

    const str SCALING_STRING = to_power_of10(SCALING / AXIS_SCALING);

    // Grid, axis & labels
    for (int x = WINDOW_MARGIN; x <= WINDOW_WIDTH - WINDOW_MARGIN; x += static_cast<int>(GRID_SPACING)) {
        float thick = 1.5;
        Color color = Fade(DARKGRAY, 0.35f);
        if (x == WINDOW_MARGIN || x == WINDOW_WIDTH - WINDOW_MARGIN) {
            thick = 2;
            color = BLACK;
        }
        DrawLine(
            Vec2(x, WINDOW_MARGIN),
            Vec2(x, WINDOW_HEIGHT - WINDOW_MARGIN),
            thick,
            color
        );
        if (x != WINDOW_WIDTH - WINDOW_MARGIN && x != WINDOW_MARGIN) {
            DrawTextCenteredEx(
                uiFont,
                round_to_hundreds(((static_cast<double>(x) / GRID_SPACING) - static_cast<double>(vertical_lines) / 2 - 0.5) * AXIS_SCALING / static_cast<int>(vertical_lines / 2)).c_str(),
                {static_cast<double>(x), static_cast<double>(WINDOW_HEIGHT - WINDOW_MARGIN + 25)},
                315,
                24,
                1,
                BLACK
            );
        }
    }

    for (int y = WINDOW_MARGIN; y <= WINDOW_HEIGHT - WINDOW_MARGIN; y += static_cast<int>(GRID_SPACING)) {
        float thick = 1.5;
        Color color = Fade(DARKGRAY, 0.35f);
        if (y == WINDOW_MARGIN || y == WINDOW_HEIGHT - WINDOW_MARGIN) {
            thick = 2;
            color = BLACK;
        }

        DrawLine(
            Vec2(WINDOW_MARGIN, y),
            Vec2(WINDOW_WIDTH - WINDOW_MARGIN, y),
            thick,
            color
        );
        if (y != WINDOW_WIDTH - WINDOW_MARGIN && y != WINDOW_MARGIN) {
            DrawTextCenteredEx(
                uiFont,
                round_to_hundreds(((static_cast<double>(y) / GRID_SPACING) - static_cast<double>(horizontal_lines) / 2 - 0.5) * AXIS_SCALING / static_cast<int>(horizontal_lines / 2)).c_str(),
                {static_cast<double>(WINDOW_MARGIN - 25), static_cast<double>(y)},
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
        {static_cast<double>(WINDOW_MARGIN) / 2 - 15, static_cast<double>(WINDOW_HEIGHT) / 2},
        -90.0f,
        24,
        1,
        BLACK
    );

    DrawTextCenteredEx(
        uiFont,
        std::format("X Position ({} m)", SCALING_STRING).c_str(),
        {static_cast<double>(WINDOW_WIDTH) / 2, WINDOW_HEIGHT - static_cast<double>(WINDOW_MARGIN) / 2 + 15},
        0,
        24,
        1,
        BLACK
    );

    // Left side
    DrawText(uiFont, std::format("Simulation time: {} years", static_cast<int>(static_cast<double>(steps_simulated) * TIME_STEP / (86'400 * 365))).c_str(), Vec2(WINDOW_MARGIN, 10), 20, 1, BLACK);
    DrawText(uiFont, std::format("Computation time: {} seconds", round_to_hundreds(timer.seconds())).c_str(), Vec2(WINDOW_MARGIN, 30), 20, 1, BLACK);
    DrawText(uiFont, std::format("Simulated years per second: {}", round_to_hundreds(std::ceil(((static_cast<double>(steps_simulated) * TIME_STEP / (86'400 * 365))) / timer.seconds()))).c_str(), Vec2(WINDOW_MARGIN, 50), 20, 1, BLACK);

    // TODO: This is an average of the entire simulation
    // It would be way cooler if it was the average of the last second, right?

    // Right side
    const std::size_t orbit_points_used = snap ? snap->max_orbit_points_used : 0;

    DrawText(uiFont, std::format("Step size: {}", TIME_STEP_STRING).c_str(), Vec2(WINDOW_MARGIN + 400, 10), 20, 1, BLACK);
    DrawText(uiFont, std::format("Maximum orbit points used: {}/{}", orbit_points_used, MAX_ORBIT_POINTS).c_str(), Vec2(WINDOW_MARGIN + 400, 30), 20, 1, BLACK);
    DrawText(uiFont, std::format("Rendering relative to: {}", celestial_bodies[center_celestial_body_index]->name).c_str(), Vec2(WINDOW_MARGIN + 400, 50), 20, 1, BLACK);
    // DrawText(uiFont, std::format("Axis scaling: {}", AXIS_SCALING).c_str(), Vec2(WINDOW_MARGIN + 400, 70), 20, 1, BLACK);
}

// Returns top right and bottom left corner
void draw_legend() {
    // Legend

    const Vec2 start = {WINDOW_WIDTH - WINDOW_MARGIN, WINDOW_MARGIN};
    const Vec2 end = {WINDOW_WIDTH - GRID_SPACING - WINDOW_MARGIN, WINDOW_MARGIN + 16 * NUM_CELESTIAL_BODIES + 10};

    DrawRectangle(start, end, WHITE);

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (const auto& celestial_body = celestial_bodies[i]; celestial_body->color.has_value()) {
            constexpr double font_size = 14;
            DrawCircle({WINDOW_WIDTH - WINDOW_MARGIN - 83, WINDOW_MARGIN + 16 * i + (font_size / 2)}, 5, *celestial_body->color);
            DrawText(uiFont, celestial_body->name.c_str(), Vec2(WINDOW_WIDTH - WINDOW_MARGIN - 75, WINDOW_MARGIN + 16 * i), font_size, 1, BLACK);
        }
    }

    DrawLine({WINDOW_WIDTH - GRID_SPACING - WINDOW_MARGIN, WINDOW_MARGIN}, {WINDOW_WIDTH - GRID_SPACING - WINDOW_MARGIN, WINDOW_MARGIN + 16 * NUM_CELESTIAL_BODIES + 10}, 2, BLACK);
    DrawLine({WINDOW_WIDTH - GRID_SPACING - WINDOW_MARGIN, WINDOW_MARGIN + 16 * NUM_CELESTIAL_BODIES + 10}, {WINDOW_WIDTH - WINDOW_MARGIN, WINDOW_MARGIN + 16 * NUM_CELESTIAL_BODIES + 10}, 2, BLACK);
}

void draw_planets(const std::shared_ptr<const RenderSnapshot>& snap) {
    // snap->bodies positions are already relative to the center body (see publish_snapshot)
    for (const auto& [position, radius, color] : snap->bodies) {
        if (color.has_value() && Vec3::inside_screen(position)) {
            DrawCircleV(position.to_raylib(), radius, *color);
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

            if (Vec3::inside_screen(start) && Vec3::inside_screen(end)) {
                DrawLineEx(
                    start.to_raylib(),
                    end.to_raylib(),
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
    const double zoom_factor = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? (ZOOM_FACTOR*ZOOM_FACTOR) : ZOOM_FACTOR;
    if (scroll > 0 || IsKeyDown(KEY_RIGHT_BRACKET)) { // "+" on QWERTZ
        // scrolled up (=> in)
        if (AXIS_SCALING / zoom_factor < 4.0/3) {
            AXIS_SCALING *= 10;
            AXIS_SCALING /= zoom_factor;
            SCALING /= zoom_factor;
        } else {
            AXIS_SCALING /= zoom_factor;
            SCALING /= zoom_factor;
        }
    } else if (scroll < 0 || IsKeyDown(KEY_SLASH)) { // "-" on QWERTZ
        // scrolled down (=> out)
        if (AXIS_SCALING * zoom_factor > 10 * 4.0/3) {
            AXIS_SCALING /= 10;
            AXIS_SCALING *= zoom_factor;
            SCALING *= zoom_factor;
        } else {
            AXIS_SCALING *= zoom_factor;
            SCALING *= zoom_factor;
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
        SCALING = ORIGINAL_SCALING;
        AXIS_SCALING = ORIGINAL_AXIS_SCALING;
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

    draw_ui(snap);
    if (snap) {
        draw_orbits(snap);
        draw_planets(snap);
    }
    draw_legend();

    EndDrawing();
}

int main() {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Umlaufbahn Simulation");
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
    printf("Simulation time: %.2f years\n", (static_cast<double>(steps_simulated) * TIME_STEP / (86'400 * 365)));
    printf("Computation time: %.2f seconds\n", timer.seconds());
    printf("Simulated years per second: %.2f\n", (static_cast<double>(steps_simulated) * TIME_STEP / (86'400 * 365)) / timer.seconds());

    return 0;
}
