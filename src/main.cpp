#include <raylib.h>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <stop_token>
#include <cmath>

// TODO: Parallel Execution
// TODO: Make sure the planets never go over the UI borders
// TODO: Orbits
// TODO: Add second timer at the top

using str = std::string;

#define NUM_CELESTIAL_BODIES 5
#define EARTH 1
#define MARS 2
#define JUPITER 3
#define SATURN 4

constexpr double SCALING = 2e12;
constexpr str SCALING_STRING = "2 x 10^12";
constexpr double time_step = 30; // TODO: Defines step size
unsigned long long steps_simulated = 0;

constexpr double GRAVITATIONAL_CONSTANT = 6.6743e-11;
constexpr int WINDOW_HEIGHT = 900;
constexpr int WINDOW_WIDTH = 900;
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

class CelestialBody {
public:
    Vec3 position;
    Vec3 velocity;
    double mass;

    CelestialBody(const Vec3 &position, const Vec3 &velocity, const double mass) : position(position), velocity(velocity), mass(mass) {}

    [[nodiscard]] double distanceTo(const CelestialBody &body) const {
        return (position - body.position).length();
    }

    [[nodiscard]] Vec3 acceleration_due_to(const CelestialBody &source) const {
        const Vec3 offset = position - source.position;
        const double distance = distanceTo(source);

        // -(G * M / r^3) * offset
        return offset * (-GRAVITATIONAL_CONSTANT * source.mass
            / (distance * distance * distance));
    }
};

CelestialBody earth = {{1.051110894240638e10, -1.524721760044149e11, 2.551204317737371e7}, {2.923284130475473e4, 2.012304925857257e3, -2.997543950911119e-1}, 5.97219e24, 10, BLUE};
CelestialBody mars = {{1.804158291514496e11, 1.155212196101544e11, -1.976959322734013e9}, {-1.217743703266605e4, 2.244555151648691e4, 7.689594977117213e2}, 6.4171e23, 10, RED};
CelestialBody jupiter = {{-4.339909949222860e11, 6.583216063749719e11, 6.981808430314541e9}, {-1.106477669156617e4, -6.573232170198393e3, 2.749669443388072e2}, 18.9819e26, 10, ORANGE};
CelestialBody saturn = {{1.402238236376539e12, 1.837816450614337e11, -5.902702510104157e10}, {-1.786665911022244e3, 9.555444950590967e3, -9.444982300526794e1}, 5.6834e26, 10, YELLOW};
CelestialBody sun   = {{0, 0, 0}, {0, 0, 0}, 1.98847e30, 10, YELLOW};

CelestialBody* celestial_bodies[NUM_CELESTIAL_BODIES] = {&sun, &earth, &mars, &jupiter, &saturn};

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
        celestial_bodies[i]->velocity += accelerations[i] * time_step;
    }

    for (auto& celestial_body : celestial_bodies) {
        celestial_body->position += celestial_body->velocity * time_step;
    }

    steps_simulated++;
}

void simulate_cpu(const std::stop_token& stop_token, std::mutex& system_lock) {
    while (!stop_token.stop_requested()) {
        simulate_step(system_lock);
    }
}

void DrawTextCenteredEx(const Font &font, const char *text, const Vector2 center, const float angle, const float fontSize, const float spacing, const Color tint) {
    auto [x, y] = MeasureTextEx(font, text, fontSize, spacing);

    DrawTextPro(
        font,
        text,
        center,
        Vector2(x/2, y/2),
        angle,
        fontSize,
        spacing,
        tint
    );
}


void draw_ui(const int spacing, const int margin) {
    const int horizontal_lines = (WINDOW_HEIGHT - 2 * margin) / spacing + 1;
    const int vertical_lines = (WINDOW_WIDTH - 2 * margin) / spacing + 1;

    // Grid, axis & labels
    // TODO: Make sth. so we can STOP SPAMMING static_cast<float>() **EVERYWHERE**
    for (int x = margin; x <= WINDOW_WIDTH - margin; x += static_cast<int>(spacing)) {
        DrawLineEx(
            Vector2(static_cast<float>(x), static_cast<float>(margin)),
            Vector2(static_cast<float>(x), static_cast<float>(WINDOW_HEIGHT) - static_cast<float>(margin)),
            1.5,
            Fade(DARKGRAY, 0.35f)
        );
        DrawTextCenteredEx(
            uiFont,
            std::to_string((x / spacing) - 1 - vertical_lines / 2).c_str(),
            {static_cast<float>(x), static_cast<float>(WINDOW_HEIGHT - margin + 15)},
            0,
            24,
            1,
            BLACK
        );
    }

    for (int y = margin; y <= WINDOW_WIDTH - margin; y += static_cast<int>(spacing)) {
        DrawLineEx(
            Vector2(static_cast<float>(margin), static_cast<float>(y)),
            Vector2(static_cast<float>(WINDOW_WIDTH) - static_cast<float>(margin), static_cast<float>(y)),
            1.5,
            Fade(DARKGRAY, 0.35f)
        );
        DrawTextCenteredEx(
            uiFont,
            std::to_string((y / spacing) - 1 - horizontal_lines / 2).c_str(),
            {static_cast<float>(margin - 15), static_cast<float>(y)},
            0,
            24,
            1,
            BLACK
        );
    }

    DrawTextCenteredEx(
        uiFont,
        std::format("Y Position ({} m)", SCALING_STRING).c_str(),
        Vector2(static_cast<float>(margin) / 2, static_cast<float>(WINDOW_HEIGHT) / 2),  // position
        -90.0f,
        24,
        1,
        BLACK
    );

    DrawTextCenteredEx(
        uiFont,
        std::format("X Position ({} m)", SCALING_STRING).c_str(),
        Vector2(static_cast<float>(WINDOW_WIDTH) / 2, WINDOW_HEIGHT - static_cast<float>(margin) / 2),
        0,
        24,
        1,
        BLACK
    );

    // TODO: Do we need a lock here?
    // Seconds passed timer
    DrawText(std::format("Years simulated: {}", static_cast<int>(static_cast<double>(steps_simulated) * time_step / (86400 * 365))).c_str(), margin, 10, 24, BLACK);
}

void draw_planets(std::mutex& system_lock) {
    Vec3 snapshots[NUM_CELESTIAL_BODIES];
    unsigned long long steps;

    {
        // copy only 2 doubles while holding lock
        std::lock_guard lock(system_lock);
        // TODO ! Automate for all

        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            snapshots[i] = celestial_bodies[i]->position;
        }
        steps = steps_simulated;
    }

    DrawCircle(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2, 20.0, YELLOW);
    DrawCircleV(snapshots[EARTH].to_raylib(), 10.0, BLUE);
    DrawCircleV(snapshots[MARS].to_raylib(), 10.0, RED);
    DrawCircleV(snapshots[JUPITER].to_raylib(), 10.0, ORANGE);
    DrawCircleV(snapshots[SATURN].to_raylib(), 10.0, YELLOW);

    // printf("seconds simulated: %f\n", static_cast<double>(steps) * time_step);
    // printf("days simulated:    %f\n", static_cast<double>(steps) * time_step / 86400);
    // printf("frames passed:     %llu\n", frame_number);
    // printf("earth:             %s\n", snapshots[EARTH].to_string().c_str());
    // printf("mars:              %s\n\n", snapshots[MARS].to_string().c_str());
}

void draw_orbits() {

}

int main() {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Umlaufbahn Simulation");
    SetTargetFPS(60.0);

    std::mutex system_lock;

    std::jthread cpu_thread(simulate_cpu, std::ref(system_lock));

    constexpr int spacing = 90;
    constexpr int margin = 1 * spacing;

    unsigned long long frame_number = 0;

    uiFont = LoadFontEx(
        "resources/JetBrainsMono-Regular.ttf",
        96,
        nullptr,
        0
    );

    GenTextureMipmaps(&uiFont.texture);
    SetTextureFilter(uiFont.texture, TEXTURE_FILTER_TRILINEAR);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(WHITE);

        draw_ui(spacing, margin);
        draw_planets(system_lock);
        draw_orbits();

        EndDrawing();
        frame_number++;
    }

    cpu_thread.request_stop();

    UnloadFont(uiFont);
    CloseWindow();

    return 0;
}
