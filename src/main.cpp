#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <raylib.h>
#include <stop_token>
#include <thread>
#include <vector>

// TODO: Parallel Execution
// TODO: Make sure the planets never go over the UI borders

using str = std::string;

#define NUM_CELESTIAL_BODIES 7
#define FPS 60.0

constexpr double SCALING = 2e12; // 2e12 matches 10^12, look at the way we handle this in draw_ui() TODO: Not communicated cleary though, maybe calculate the string on the run?
constexpr str SCALING_STRING = "10^12";
constexpr double TIME_STEP = 900; // TODO: Defines step size
constexpr str TIME_STEP_STRING = "15 mins";
constexpr bool RENDERING_COORDINATES_RELATIVE_TO_SUN = true;

constexpr double GRAVITATIONAL_CONSTANT = 6.6743e-11;
constexpr int WINDOW_HEIGHT = 900;
constexpr int WINDOW_WIDTH = 900;
constexpr int MAX_ORBIT_POINTS = 10'000;
constexpr int ORBIT_SAMPLE_EVERY_STEPS = 3000; // TODO: Defines how often orbit samples are taken

Font uiFont;

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
            static_cast<float>((x / SCALING) / 2.0 + 0.5) * WINDOW_WIDTH,
            static_cast<float>((y / SCALING) / 2.0 + 0.5) * WINDOW_HEIGHT
        };
    }
};

class Vec2 {
public:
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    Vec2(const double x, const double y) : x(x), y(y) {}
};

class CelestialBody {
public:
    Vec3 position;
    Vec3 velocity;
    double mass;
    float radius;
    std::optional<Color> color;

    CelestialBody(const Vec3 &position, const Vec3 &velocity, const double mass, const float radius, const std::optional<Color>& color) : position(position), velocity(velocity), mass(mass), radius(radius), color(color) {}

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

CelestialBody mercury = {{-3.229439434041441e10, -6.212384097453145e10, -2.058972617997836e9}, {3.337844168997828e4, -2.019057755964253e4, -4.710888632749314e3}, 3.302e23, 5, BLUE};
CelestialBody venus = {{-1.019802701272269e11, -3.651886112603541e10, 5.393515277422819e9}, {1.137949480576350e4, -3.319873100002852e4, -1.112329309713790e3}, 48.685e23, 5, YELLOW};
CelestialBody earth = {{1.051110894240638e10, -1.524721760044149e11, 2.551204317737371e7}, {2.923284130475473e4, 2.012304925857257e3, -2.997543950911119e-1}, 5.97219e24, 5, BLUE};
CelestialBody mars = {{1.804158291514496e11, 1.155212196101544e11, -1.976959322734013e9}, {-1.217743703266605e4, 2.244555151648691e4, 7.689594977117213e2}, 6.4171e23, 10, RED};
CelestialBody jupiter = {{-4.339909949222860e11, 6.583216063749719e11, 6.981808430314541e9}, {-1.106477669156617e4, -6.573232170198393e3, 2.749669443388072e2}, 18.9819e26, 10, ORANGE};
CelestialBody saturn = {{1.402238236376539e12, 1.837816450614337e11, -5.902702510104157e10}, {-1.786665911022244e3, 9.555444950590967e3, -9.444982300526794e1}, 5.6834e26, 10, YELLOW};
// CelestialBody uranus = {{1.386689779015193e12, 2.558518371479970e12, -8.462715242429852e9}, {-6.037314354491155e3, 2.927562363958545e3, 8.916577001311432e1}, 86.813e24, 10, BLUE};
// CelestialBody neptune = {{4.465613570420511e12, 1.599153765150425e11, -1.062078323030064e11}, {-2.302777319654876e2, 5.463337689178736e3, -1.078268539063705e2}, 102.409e24, 10, BLUE};
// CelestialBody pluto = {{2.947351321346399e12, -4.409737094120408e12, -3.806824198825967e11}, {4.656489570352034e3, 1.788853980502755e3, -1.545806860243079e3}, 1.307e22, 10, BROWN};
CelestialBody sun   = {{0, 0, 0}, {0, 0, 0}, 1.98847e30, 10, YELLOW};

// Always leave sun at index 0 - other code depends on sun.position
CelestialBody* celestial_bodies[NUM_CELESTIAL_BODIES] = {&sun, &mercury, &venus, &earth, &mars, &jupiter, &saturn};
std::array<std::deque<Vec3>, NUM_CELESTIAL_BODIES> orbit_history;

std::atomic<std::size_t> frame_number = 0;
std::atomic<std::size_t> steps_simulated = 0;

void save_orbit_points() {
    if (steps_simulated % ORBIT_SAMPLE_EVERY_STEPS != 0) return;

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        auto& history = orbit_history[i];

        history.push_back(celestial_bodies[i]->position);

        if (history.size() > MAX_ORBIT_POINTS) {
            // Delete 10% of MAX_ORBIT_POINTS
            for (int j = 0; j < MAX_ORBIT_POINTS / 10; j++) {
                history.pop_front();
            }
        }
    }
}

void simulate_step(std::mutex& system_lock) {
    std::lock_guard lock(system_lock);

    Vec3 accelerations[NUM_CELESTIAL_BODIES] = {};

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        for (int j = 0; j < NUM_CELESTIAL_BODIES; j++) {
            if (i == j) continue; // Do not apply gravity from this object to this
            accelerations[i] += celestial_bodies[i]->acceleration_due_to(*celestial_bodies[j]);
        }
    }

    // TODO: Apparently it's more stable to update velocity before updating the position?
    // TODO: Why?

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        celestial_bodies[i]->velocity += accelerations[i] * TIME_STEP;
    }

    for (auto& celestial_body : celestial_bodies) {
        celestial_body->position += celestial_body->velocity * TIME_STEP;
    }

    ++steps_simulated;
    save_orbit_points();
}

void simulate_cpu(const std::stop_token& stop_token, std::mutex& system_lock) {
    while (!stop_token.stop_requested()) {
        simulate_step(system_lock);
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

void DrawText(const Font &font, const char *text, const Vec2 &position, const float fontSize, const float spacing, const Color color) {
    DrawTextEx(font, text, Vector2(static_cast<float>(position.x), static_cast<float>(position.y)), fontSize, spacing, color);
}

void DrawLine(const Vec2& start_pos, const Vec2& end_pos, const float thick, const Color color) {
    DrawLineEx(Vector2(static_cast<float>(start_pos.x), static_cast<float>(start_pos.y)), Vector2(static_cast<float>(end_pos.x), static_cast<float>(end_pos.y)), thick, color);
}

void draw_ui(const int spacing, const int margin, std::mutex& system_lock) {
    const int horizontal_lines = (WINDOW_HEIGHT - 2 * margin) / spacing + 1;
    const int vertical_lines = (WINDOW_WIDTH - 2 * margin) / spacing + 1;

    // Grid, axis & labels
    for (int x = margin; x <= WINDOW_WIDTH - margin; x += static_cast<int>(spacing)) {
        DrawLine(
            Vec2(x, margin),
            Vec2(x, WINDOW_HEIGHT - margin),
            1.5,
            Fade(DARKGRAY, 0.35f)
        );
        if (x != WINDOW_WIDTH - margin && x != margin) {
            DrawTextCenteredEx(
                uiFont,
                std::to_string(((x / spacing) - 1 - vertical_lines / 2) * 2).c_str(),
                {static_cast<double>(x), static_cast<double>(WINDOW_HEIGHT - margin + 15)},
                0,
                24,
                1,
                BLACK
            );
        }
    }

    for (int y = margin; y <= WINDOW_WIDTH - margin; y += static_cast<int>(spacing)) {
        DrawLine(
            Vec2(margin, y),
            Vec2(WINDOW_WIDTH - margin, y),
            1.5,
            Fade(DARKGRAY, 0.35f)
        );
        if (y != WINDOW_WIDTH - margin && y != margin) {
            DrawTextCenteredEx(
                uiFont,
                std::to_string(((y / spacing) - 1 - horizontal_lines / 2) * 2).c_str(),
                {static_cast<double>(margin - 15), static_cast<double>(y)},
                0,
                24,
                1,
                BLACK
            );
        }
    }

    DrawTextCenteredEx(
        uiFont,
        std::format("Y Position ({} m)", SCALING_STRING).c_str(),
        {static_cast<double>(margin) / 2, static_cast<double>(WINDOW_HEIGHT) / 2},
        -90.0f,
        24,
        1,
        BLACK
    );

    DrawTextCenteredEx(
        uiFont,
        std::format("X Position ({} m)", SCALING_STRING).c_str(),
        {static_cast<double>(WINDOW_WIDTH) / 2, WINDOW_HEIGHT - static_cast<double>(margin) / 2},
        0,
        24,
        1,
        BLACK
    );

    unsigned long long orbit_points_used = 0;

    {
        std::lock_guard lock(system_lock);
        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            const auto& orbit = orbit_history[i];
            orbit_points_used = orbit_points_used > orbit.size() ? orbit_points_used : orbit.size();
        }
    }

    const str step_size_text = std::format("Step size: {}", TIME_STEP_STRING);

    DrawText(uiFont, step_size_text.c_str(), Vec2(margin, 10), 20, 1, BLACK);
    DrawText(uiFont, std::format("Years simulated: {}", static_cast<int>(static_cast<double>(steps_simulated) * TIME_STEP / (86400 * 365))).c_str(), Vec2(margin, 30), 20, 1, BLACK);
    DrawText(uiFont, std::format("Years / second: {}", static_cast<int>(((static_cast<double>(steps_simulated) * TIME_STEP / (86400 * 365))) / (static_cast<double>(frame_number) / FPS))).c_str(), Vec2(margin, 50), 20, 1, BLACK);
    DrawText(uiFont, std::format("Maximum orbit points used: {}/{}", orbit_points_used, MAX_ORBIT_POINTS).c_str(), Vec2(static_cast<double>(margin + 250), 10), 20, 1, BLACK);
    DrawText(uiFont, std::format("Rendering relative to sun: {}", RENDERING_COORDINATES_RELATIVE_TO_SUN).c_str(), Vec2(margin + 250, 30), 20, 1, BLACK);
}

void draw_planets(std::mutex& system_lock) {
    struct PlanetSnapshot {
        Vec3 position;
        float radius{};
        std::optional<Color> color{};
    };

    PlanetSnapshot snapshots[NUM_CELESTIAL_BODIES];

    {
        // copy only doubles while holding lock
        std::lock_guard lock(system_lock);

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            snapshots[i] = {
                celestial_bodies[i]->position,
                celestial_bodies[i]->radius,
                celestial_bodies[i]->color
            };
        }
    }

    const Vec3 sun_position = snapshots[0].position;

    for (const auto&[position, radius, color] : snapshots) {
        if (color.has_value()) {
            Vec3 relative_pos = position;

            if (RENDERING_COORDINATES_RELATIVE_TO_SUN) {
                relative_pos -= sun_position;
            }

            DrawCircleV(relative_pos.to_raylib(), radius, *color);
        }
    }

    // printf("seconds simulated: %f\n", static_cast<double>(steps) * time_step);
    // printf("days simulated:    %f\n", static_cast<double>(steps) * time_step / 86400);
    // printf("frames passed:     %llu\n", frame_number);
    // printf("earth:             %s\n", snapshots[EARTH].to_string().c_str());
    // printf("mars:              %s\n\n", snapshots[MARS].to_string().c_str());

    // printf("sun: %s\n", snapshots[0].position.to_string().c_str());
}

void draw_orbits(std::mutex& system_lock) {
    std::array<std::pair<std::vector<Vec3>, std::optional<Color>>, NUM_CELESTIAL_BODIES> snapshots;

    {
        std::lock_guard lock(system_lock);

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++)
        {
            snapshots[i] = {
                {
                    orbit_history[i].begin(),
                    orbit_history[i].end()
                }, celestial_bodies[i]->color
            };
        }
    }

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const auto& [points, color] = snapshots[i];

        for (unsigned long long j = 1; j < points.size(); j++) {
            const auto alpha = static_cast<float>(static_cast<double>(j) * (-0.7 / (MAX_ORBIT_POINTS) / 2) + 0.8);

            if (color.has_value()) {
                Vec3 relative_start = points[j - 1];
                Vec3 relative_end = points[j];

                if (RENDERING_COORDINATES_RELATIVE_TO_SUN) {
                    relative_start -= snapshots[0].first[j];
                    relative_end -= snapshots[0].first[j];
                }

                DrawLineEx(
                    relative_start.to_raylib(),
                    relative_end.to_raylib(),
                    1.5f,
                    Fade(*color, alpha)
                );
            }
        }
    }
}

int main() {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Umlaufbahn Simulation");
    SetTargetFPS(FPS);

    std::mutex system_lock;

    std::jthread cpu_thread(simulate_cpu, std::ref(system_lock));

    constexpr int spacing = 90;
    constexpr int margin = 1 * spacing;

    uiFont = LoadFontEx(
        "../resources/JetBrainsMono-Regular.ttf",
        96,
        nullptr,
        0
    );

    GenTextureMipmaps(&uiFont.texture);
    SetTextureFilter(uiFont.texture, TEXTURE_FILTER_TRILINEAR);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(WHITE);

        draw_ui(spacing, margin, system_lock);
        draw_orbits(system_lock);
        draw_planets(system_lock);

        EndDrawing();
        ++frame_number;
    }

    cpu_thread.request_stop();

    UnloadFont(uiFont);
    CloseWindow();

    return 0;
}
