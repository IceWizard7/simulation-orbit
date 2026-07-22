#include "ui.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <mutex>
#include <raymath.h>
#include <rlgl.h>

#include "screen_utils.hpp"
#include "simulation.hpp"

namespace ui_config {
    float cam_azimuth = config::DEFAULT_CAM_AZIMUTH; // 0
    
    float cam_elevation = config::DEFAULT_CAM_ELEVATION; // 35° shows 3D immediately
    float cam_distance = config::DEFAULT_CAM_DISTANCE; // radius in world units
    bool view_3d = false; // toggle with keybind
    bool view_legend = true; // toggle with keybind
    auto last_copied = std::chrono::steady_clock::now();
    bool copied = false;
    bool draw_moon_orbits = false;
    bool draw_orbits = true;
}

namespace {
    constexpr float CAMERA_FOVY = 45.0f;
    constexpr float MIN_CAM_DISTANCE = 1.0e-6f;
    constexpr float MAX_CAM_DISTANCE = 200.0f;

    constexpr float PLANET_MIN_RADIUS_PX = 6.0f;
    constexpr float PLANET_MAX_RADIUS_PX = 30.0f;
    constexpr float MOON_MIN_RADIUS_PX = 3.0f;
    constexpr float MOON_MAX_RADIUS_PX = 10.0f;

    Model star_model{};
    Texture2D star_texture{};
    bool star_background_loaded = false;

    [[nodiscard]] bool is_moon(const int body_index) {
        return body_index >= NUM_PLANETS + NUM_DWARF_PLANETS;
    }

    [[nodiscard]] float projected_radius_pixels(
        const Vector3& world_position,
        const float world_radius,
        const Camera3D& cam
    ) {
        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
        const Vector2 center_screen = GetWorldToScreen(world_position, cam);
        const Vector2 edge_screen = GetWorldToScreen(
            Vector3Add(world_position, Vector3Scale(right, world_radius)),
            cam
        );

        return Vector2Distance(center_screen, edge_screen);
    }

    [[nodiscard]] float adaptive_body_radius_3d(
        const ui::RenderSnapshot::Body& body,
        const int body_index,
        const Camera3D& cam
    ) {
        const Vector3 world_position = to_world(body.position);
        const float projected_radius = projected_radius_pixels(world_position, body.radius_3d, cam);
        if (!std::isfinite(projected_radius) || projected_radius <= 0.0f) return body.radius_3d;

        const float min_radius = is_moon(body_index) ? MOON_MIN_RADIUS_PX : PLANET_MIN_RADIUS_PX;
        const float max_radius = is_moon(body_index) ? MOON_MAX_RADIUS_PX : PLANET_MAX_RADIUS_PX;
        const float desired_radius = std::clamp(projected_radius, min_radius, max_radius);
        return body.radius_3d * desired_radius / projected_radius;
    }

    [[nodiscard]] double camera_near_plane(const Camera3D& cam) {
        return std::max(1.0e-9, static_cast<double>(Vector3Distance(cam.position, cam.target)) * 1.0e-3);
    }

    [[nodiscard]] double camera_far_plane(
        const Camera3D& cam,
        const std::shared_ptr<const ui::RenderSnapshot>& snap
    ) {
        double far_plane = std::max(camera_near_plane(cam) * 1.0e6, 1.0e-6);
        if (!snap) return far_plane;

        constexpr double DEPTH_MARGIN = 1.05;
        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            const auto& body = snap->bodies[i];
            if (!body.enabled) continue;

            const Vector3 world_position = to_world(body.position);
            const double center_depth = Vector3DotProduct(
                Vector3Subtract(world_position, cam.position),
                forward
            );
            const double outer_depth = center_depth + adaptive_body_radius_3d(body, i, cam);
            if (!std::isfinite(outer_depth) || outer_depth <= 0.0) continue;

            far_plane = std::max(far_plane, outer_depth * DEPTH_MARGIN);
        }

        return far_plane;
    }

    void fit_camera_to_planetary_system(
        const std::shared_ptr<const ui::RenderSnapshot>& snap,
        const int center_index
    ) {
        if (!snap) return;

        const auto system = std::ranges::find_if(
            simulation::planetary_systems,
            [center_index](const PlanetarySystem& candidate) {
                return candidate.parent_index == center_index;
            }
        );
        if (system == simulation::planetary_systems.end()) return;

        const Vec3 parent_position = snap->bodies[center_index].position;
        double system_radius_meters = 0.0;
        for (const int moon_index : system->moon_indices) {
            const auto& moon = snap->bodies[moon_index];
            if (!moon.enabled) continue;
            system_radius_meters = std::max(
                system_radius_meters,
                (moon.position - parent_position).length()
            );
        }
        if (system_radius_meters <= 0.0) return;

        // Keep the outermost modeled moon within 70% of the vertical half-view.
        constexpr double VIEW_FILL = 0.70;
        const double system_radius_world = system_radius_meters / config::WORLD_UNIT_METERS;
        constexpr double half_vertical_fov = CAMERA_FOVY * DEG2RAD / 2.0;
        const double fitted_distance = system_radius_world / (VIEW_FILL * std::tan(half_vertical_fov));
        ui_config::cam_distance = std::clamp(
            static_cast<float>(fitted_distance),
            MIN_CAM_DISTANCE,
            MAX_CAM_DISTANCE
        );
    }

    void draw_star_background(const Camera3D& cam, const double far_plane) {
        if (!star_background_loaded) return;

        const auto radius = static_cast<float>(far_plane * 0.5);

        // We are looking at the sphere from inside
        rlDisableBackfaceCulling();


        // The sky must not hide planets drawn afterwards
        rlDisableDepthMask();

        DrawModelEx(
            star_model,
            cam.position,
            {0.0f, 1.0f, 0.0f},
            0.0f,
            {radius, radius, radius},
            WHITE
        );

        // Enable rlgl-stuff again
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
    }
}


void ui::load_star_background() {
    star_model = LoadModelFromMesh(make_equirectangular_sphere_mesh(1.0f, 64, 128));
    star_texture = LoadTexture("resources/8k_stars_milky_way.jpg");
    // Alternative: "resources/stars.jpg"

    if (star_texture.id == 0) {
        std::cerr << "Could not load star background.\n";
        UnloadModel(star_model);
        star_model = {};
        return;
    }

    GenTextureMipmaps(&star_texture);
    SetTextureFilter(star_texture, TEXTURE_FILTER_TRILINEAR); // smooth sampling between pixels
    SetMaterialTexture(
        &star_model.materials[0],
        MATERIAL_MAP_ALBEDO,
        star_texture
    );

    star_background_loaded = true;
}

void ui::unload_star_background() {
    if (!star_background_loaded) return;

    UnloadTexture(star_texture);
    UnloadModel(star_model);

    star_texture = {};
    star_model = {};
    star_background_loaded = false;
}

void ui::DrawTextCenteredEx(const Font &font, const char *text, const Vec2 center, const float angle, const float fontSize, const float spacing, const Color color) {
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

void ui::DrawRectangle(const Vec2& a, const Vec2& b, const Color color) {
    const auto left = static_cast<float>(std::min(a.x, b.x));
    const auto top = static_cast<float>(std::min(a.y, b.y));
    const auto width = static_cast<float>(std::abs(b.x - a.x));
    const auto height = static_cast<float>(std::abs(b.y - a.y));

    DrawRectangleV({left, top}, {width, height}, color);
}

void ui::DrawTextOutlined(const Font& font, const char* text, const Vector2& pos, const float fontSize, const float spacing, const Color& fill, const Color& outline) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx || dy) {
                DrawTextEx(font, text, {pos.x + static_cast<float>(dx), pos.y + static_cast<float>(dy)}, fontSize, spacing, outline);
            }
        }
    }
    DrawTextEx(font, text, pos, fontSize, spacing, fill); // colored fill on top
}

void ui::draw_ui() {
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
                config::uiFont,
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
                config::uiFont,
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
        config::uiFont,
        std::format("Y Position ({} m)", SCALING_STRING).c_str(),
        {static_cast<double>(config::WINDOW_MARGIN) / 2 - 15, static_cast<double>(config::WINDOW_HEIGHT) / 2},
        -90.0f,
        24,
        1,
        BLACK
    );

    DrawTextCenteredEx(
        config::uiFont,
        std::format("X Position ({} m)", SCALING_STRING).c_str(),
        {static_cast<double>(config::WINDOW_WIDTH) / 2, config::WINDOW_HEIGHT - static_cast<double>(config::WINDOW_MARGIN) / 2 + 15},
        0,
        24,
        1,
        BLACK
    );
}

void ui::draw_legend(const std::shared_ptr<const RenderSnapshot>& snap) {
    constexpr double font_size = 16;
    constexpr double spacing = font_size + 2;
    const Vec2 start = {config::WINDOW_WIDTH - config::WINDOW_MARGIN * 1.25 - config::GRID_SPACING, config::WINDOW_MARGIN};
    const Vec2 end = {config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN + spacing * runtime_config::enabled_celestial_bodies};

    DrawRectangle(start, end, WHITE);

    int completed_iterations = 0;

    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (const auto& body = snap->bodies[i]; body.color.has_value()) {
            DrawCircle({start.x + 10, start.y + spacing * completed_iterations + (font_size / 2)}, 5, *body.color);
            DrawText(config::uiFont, body.name.c_str(), Vec2(start.x + 25, start.y + spacing * completed_iterations), font_size, 1, BLACK);
            completed_iterations++;
        }
    }

    constexpr float thick = 1.2f;

    DrawLine({start.x, start.y}, {end.x, start.y}, thick, BLACK);
    DrawLine({start.x, start.y}, {start.x, end.y}, thick, BLACK);
    DrawLine({start.x, end.y}, {end.x, end.y}, thick, BLACK);
    DrawLine({end.x, start.y}, {end.x, end.y}, thick, BLACK);
}

void ui::draw_planet_info(const Color text_color, const Color background_color, const std::shared_ptr<const RenderSnapshot>& snap) {
    if (config::planet_info_display_index == -1) return;

    const auto& body = snap->detailed_body_display;
    if (!body.has_value()) return;

    DrawRectangle(config::WINDOW_MARGIN, config::WINDOW_MARGIN, 5.5 * config::GRID_SPACING, 1.5 * config::GRID_SPACING, background_color);
    DrawLine({config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN}, 2, text_color);
    DrawLine({config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 1.5 * config::GRID_SPACING}, 2, text_color);
    DrawLine({config::WINDOW_MARGIN, config::WINDOW_MARGIN + 1.5 * config::GRID_SPACING}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 1.5 * config::GRID_SPACING}, 2, text_color);
    DrawLine({config::WINDOW_MARGIN, config::WINDOW_MARGIN}, {config::WINDOW_MARGIN, config::WINDOW_MARGIN + 1.5 * config::GRID_SPACING}, 2, text_color);

    DrawText(config::uiFont, "Celestial body information", {config::WINDOW_MARGIN + 15, config::WINDOW_MARGIN + 10}, 20, 1, text_color);

    DrawText(config::uiFont, "Use as center", {config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 10}, 20, 1, text_color);
    if (ui_config::copied && std::chrono::steady_clock::now() - ui_config::last_copied < std::chrono::milliseconds(1000)) {
        DrawText(config::uiFont, "Copied!", {config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}, 20, 1, text_color);
    } else {
        DrawText(config::uiFont, "Copy info", {config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}, 20, 1, text_color);
    }

    DrawCircle({config::WINDOW_MARGIN + 22.5, config::WINDOW_MARGIN + 60}, 7.5, *body->color);
    if (1 <= config::planet_info_display_index && config::planet_info_display_index <= 8) {
        DrawText(config::uiFont, std::format("{} ({}{} planet from sun)", body->name, config::planet_info_display_index.load(), get_numerical_suffix(config::planet_info_display_index)).c_str(), {config::WINDOW_MARGIN + 35, config::WINDOW_MARGIN + 50}, 20, 1, text_color);
    } else {
        DrawText(config::uiFont, std::format("{} (index {})", body->name, config::planet_info_display_index.load()).c_str(), {config::WINDOW_MARGIN + 35, config::WINDOW_MARGIN + 50}, 20, 1, text_color);
    }
    DrawText(config::uiFont, std::format("Position: {:<34}m", body->position.to_string()).c_str(), {config::WINDOW_MARGIN + 15, config::WINDOW_MARGIN + 70}, 20, 1, text_color);
    DrawText(config::uiFont, std::format("Velocity: {:<34}m/s", body->velocity.to_string()).c_str(), {config::WINDOW_MARGIN + 15, config::WINDOW_MARGIN + 90}, 20, 1, text_color);
    DrawText(config::uiFont, std::format("Mass: {:<38}kg", body->mass).c_str(), {config::WINDOW_MARGIN + 15, config::WINDOW_MARGIN + 110}, 20, 1, text_color);
}


void ui::draw_stats(const Color text_color, const Color background_color, const std::shared_ptr<const RenderSnapshot>& snap) {
    const double seconds = config::timer.seconds();

    DrawRectangle(0, 0, config::WINDOW_WIDTH, config::WINDOW_MARGIN, background_color);

    // Left side
    DrawText(config::uiFont, std::format("Simulation time: {} years", static_cast<int>(static_cast<double>(config::steps_simulated) * runtime_config::time_step / (86'400 * 365))).c_str(), Vec2(config::WINDOW_MARGIN, 10), 20, 1, text_color);
    DrawText(config::uiFont, std::format("Computation time: {} seconds", round_to_hundreds(seconds)).c_str(), Vec2(config::WINDOW_MARGIN, 30), 20, 1, text_color);
    DrawText(config::uiFont, std::format("Step size: {}", runtime_config::time_step_string).c_str(), Vec2(config::WINDOW_MARGIN, 50), 20, 1, text_color);
    // DrawText(config::uiFont, std::format("FPS: {}", GetFPS()).c_str(), Vec2(config::WINDOW_MARGIN, 70), 20, 1, text_color);

    // Right side
    DrawText(config::uiFont, std::format("Simulated years per second: {}", (seconds > 0.0 ? round_to_hundreds(((static_cast<double>(config::steps_simulated) * runtime_config::time_step / (86'400 * 365))) / seconds) : "0")).c_str(), Vec2(config::WINDOW_MARGIN + 400, 10), 20, 1, text_color);
    DrawText(config::uiFont, std::format("Rendering relative to: {}", snap->bodies[config::center_celestial_body_index].name).c_str(), Vec2(config::WINDOW_MARGIN + 400, 30), 20, 1, text_color);
    DrawText(config::uiFont, std::format("Target years per second: {}", runtime_config::target_simulation_speed > 0.0 ? round_to_hundreds(runtime_config::target_simulation_speed) : "Unlimited").c_str(), Vec2(config::WINDOW_MARGIN + 400, 50), 20, 1, text_color);
}

void ui::draw_planets(const std::shared_ptr<const RenderSnapshot>& snap) {
    const int hovered_body_i = pick_body_at_mouse_2d(snap);

    // snap->bodies positions are already relative to the center body (see publish_snapshot)
    for (int i = 0; i < snap->bodies.size(); i++) {
        const auto& [name, position, radius_2d, _radius_3d, color, _planet_visual, _enabled] = snap->bodies[i];
        if (color.has_value() && inside_screen(position)) {
            const Vector2 pos = to_raylib(position);
            DrawCircleV(pos, radius_2d, *color);
            Vector2 text_pos = pos;
            text_pos.y -= 10;
            text_pos.x += 10;
            if (hovered_body_i != -1 && i == hovered_body_i) {
                DrawTextOutlined(config::uiFont, name.c_str(), text_pos, 20, 1, *color, {0, 0, 0, 125});
            }
        }
    }
}

void ui::draw_orbits(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (!ui_config::draw_orbits) return;

    const int orbit_max_i = ui_config::draw_moon_orbits ? NUM_CELESTIAL_BODIES : NUM_PLANETS + NUM_DWARF_PLANETS;
    for (int i = 0; i < orbit_max_i; i++) {
        const auto& [points, color] = snap->orbits[i];
        if (!color.has_value() || points.size() < 2) continue;

        const auto segment_count = static_cast<float>(points.size() - 1);

        for (std::size_t j = 1; j < points.size(); j++) {
            const float age = static_cast<float>(j) / segment_count;
            const float alpha = 0.10f + 0.70f * age;

            const Vec3 start = points[j - 1];
            const Vec3 end = points[j];

            // For smoother edges, it's now enough if only 1 point is inside (aggressive DrawRectangle deals with the rest)
            if (inside_screen(start) || inside_screen(end)) {
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

void ui::draw_orbits_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
    if (!ui_config::draw_orbits) return;

    const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

    const int orbit_max_i = ui_config::draw_moon_orbits ? NUM_CELESTIAL_BODIES : NUM_PLANETS + NUM_DWARF_PLANETS;
    for (int i = 0; i < orbit_max_i; i++) {
        const auto& [pts, color] = snap->orbits[i];
        if (!color || pts.size() < 2) continue;

        const auto segment_count = static_cast<float>(pts.size() - 1);

        // GetWorldToScreen doesn't clip points behind the camera
        // it can project them to a wrong on-screen spot, so skip those segments
        const Vector3 first_world = to_world(pts[0]);
        bool prev_in_front = Vector3DotProduct(Vector3Subtract(first_world, cam.position), forward) > 0;
        Vector2 prev_screen = prev_in_front ? GetWorldToScreen(first_world, cam) : Vector2{};

        for (size_t j = 1; j < pts.size(); j++) {
            const Vector3 world = to_world(pts[j]);
            const bool in_front = Vector3DotProduct(Vector3Subtract(world, cam.position), forward) > 0;
            const Vector2 screen = in_front ? GetWorldToScreen(world, cam) : Vector2{};

            if (prev_in_front && in_front) {
                const float alpha = 0.10f + 0.70f * (static_cast<float>(j) / segment_count);
                DrawLineEx(prev_screen, screen, 1.0f, Fade(*color, alpha));
            }

            prev_in_front = in_front;
            prev_screen = screen;
        }
    }
}

void ui::draw_planets_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
    for (int i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const auto& [name, pos, _radius_2d, _radius_3d, color, planet_visual, _enabled] = snap->bodies[i];
        const float radius_3d = adaptive_body_radius_3d(snap->bodies[i], i, cam);
        if (planet_visual != nullptr && planet_visual->loaded) {
            DrawModel(
                planet_visual->model,
                to_world(pos),
                radius_3d,
                WHITE // don't tint texture
            );
        }
        else if (color.has_value()) {
            // fallback for bodies without a texture
            DrawSphereEx(to_world(pos), radius_3d, 12, 12, *color);
        }
    }
}

void ui::draw_grid_3d(const Camera3D& cam) {
    constexpr int half_grid = 5;
    const auto near_epsilon = static_cast<float>(camera_near_plane(cam));

    const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

    auto draw_clipped_line = [&](Vector3 a, Vector3 b) {
        const float depth_a = Vector3DotProduct(Vector3Subtract(a, cam.position), forward);
        const float depth_b = Vector3DotProduct(Vector3Subtract(b, cam.position), forward);

        // Entire segment is behind the camera
        if (depth_a <= near_epsilon && depth_b <= near_epsilon) {
            return;
        }

        // Clip the endpoint that is behind the camera.
        if (depth_a <= near_epsilon || depth_b <= near_epsilon) {
            const float t = (near_epsilon - depth_a) / (depth_b - depth_a);

            const Vector3 clipped = Vector3Add(a, Vector3Scale(Vector3Subtract(b, a), t));

            if (depth_a <= near_epsilon) {
                a = clipped;
            } else {
                b = clipped;
            }
        }

        DrawLineEx(
            GetWorldToScreen(a, cam),
            GetWorldToScreen(b, cam),
            0.5f,
            Fade(WHITE, 0.5f)
        );
    };

    for (int i = -half_grid; i <= half_grid; ++i) {
        constexpr float spacing = 5.0f;
        const float p = static_cast<float>(i) * spacing;

        // lines running along Z
        draw_clipped_line(
            {p, 0.0f, -half_grid * spacing},
            {p, 0.0f,  half_grid * spacing}
        );

        // lines running along X
        draw_clipped_line(
            {-half_grid * spacing, 0.0f, p},
            { half_grid * spacing, 0.0f, p}
        );
    }
}

void ui::draw_axes_3d() {
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

void ui::draw_axes_labels_3d(const Camera3D& cam) {
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
            DrawText(config::uiFont, text.c_str(), {x, y}, 14, 1, LIGHTGRAY);
        }
    };

    label_axis([](const int k){return Vector3{static_cast<float>(k), 0, 0};}, "x");
    label_axis([](const int k){return Vector3{0, 0, static_cast<float>(k)};}, "x"); // physics y -> raylib z
    label_axis([](const int k){return Vector3{0, static_cast<float>(k), 0};}, "x"); // raylib z -> raylib y
}


int ui::pick_body_at_mouse_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
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

int ui::pick_body_at_mouse_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
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

        const float rendered_radius = adaptive_body_radius_3d(body, i, cam);
        const Vector3 edge = Vector3Add(world, Vector3Scale(right, rendered_radius));
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

void ui::draw_planet_labels_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
    const int hovered_body_i = pick_body_at_mouse_3d(snap, cam);

    if (hovered_body_i == -1) return;

    const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

    const auto& [name, pos, _radius_2d, _radius_3d, color, _planet_visual, _enabled] = snap->bodies[hovered_body_i];
    if (!color.has_value()) return;

    const Vector3 world = to_world(pos);
    // GetWorldToScreen doesn't clip points behind the camera
    // it can project them to a wrong on-screen spot, so skip those segments
    if (Vector3DotProduct(Vector3Subtract(world, cam.position), forward) <= 0) return;

    Vector2 text_pos = GetWorldToScreen(world, cam);
    text_pos.y -= 10;
    text_pos.x += 10;
    DrawTextOutlined(config::uiFont, name.c_str(), text_pos, 20, 1, *color, {0, 0, 0, 125});
}

Camera3D ui::make_camera() {
    Camera3D c{};
    c.target = {0, 0, 0};
    c.up = {0, 1, 0};
    c.fovy = CAMERA_FOVY;
    c.projection = CAMERA_PERSPECTIVE; // alternative: CAMERA_ORTHOGRAPHIC
    c.position = {
        ui_config::cam_distance * cosf(ui_config::cam_elevation) * cosf(ui_config::cam_azimuth),
        ui_config::cam_distance * sinf(ui_config::cam_elevation),
        ui_config::cam_distance * cosf(ui_config::cam_elevation) * sinf(ui_config::cam_azimuth)
    };

    return c;
}

void ui::draw_3d(const std::shared_ptr<const RenderSnapshot>& snap) {
    const Camera3D cam = make_camera();
    const double far_plane = camera_far_plane(cam, snap);
    rlSetClipPlanes(camera_near_plane(cam), far_plane);

    BeginMode3D(cam);
    draw_star_background(cam, far_plane);
    EndMode3D();

    // TODO: Make orbits & planets overlap correctly, according to real perspective

    if (snap) draw_orbits_3d(snap, cam);
    // draw_grid_3d(cam);

    BeginMode3D(cam);
    if (snap) draw_planets_3d(snap, cam);
    EndMode3D();

    // 2D overlays, projected by 3D points
    if (snap) {
        draw_planet_labels_3d(snap, cam);
        draw_stats(WHITE, BLACK, snap);
        draw_planet_info(WHITE, BLACK, snap);
    }
}


void ui::draw_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (snap) {
        draw_orbits(snap);
        draw_planets(snap);
        DrawRectangle(0, 0, config::WINDOW_MARGIN, config::WINDOW_HEIGHT, WHITE);
        DrawRectangle(0, 0, config::WINDOW_WIDTH, config::WINDOW_MARGIN, WHITE);
        DrawRectangle(0, config::WINDOW_HEIGHT - config::WINDOW_MARGIN, config::WINDOW_WIDTH, config::WINDOW_MARGIN, WHITE);
        DrawRectangle(config::WINDOW_WIDTH - config::WINDOW_MARGIN, 0, config::WINDOW_MARGIN, config::WINDOW_HEIGHT, WHITE);
        draw_stats(BLACK, WHITE, snap);
        draw_ui();
        if (ui_config::view_legend) draw_legend(snap);
        draw_planet_info(BLACK, WHITE, snap);
    }
}

void ui::UpdateDrawFrame() {
    std::shared_ptr<const RenderSnapshot> snap;
    {
        std::lock_guard lock(config::snapshot_lock);
        snap = config::latest_snapshot;
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
    bool copied_button_pressed = false;

    if (config::planet_info_display_index != -1 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        auto [center_start, center_end] = center_button_coordinates();
        auto [copy_start, copy_end] = copy_button_coordinates();

        const auto [mx, my] = GetMousePosition();

        if (center_start.x <= mx && mx <= center_end.x &&
            center_start.y <= my && my <= center_end.y) {
            center_button_pressed = true;
        }

        if (copy_start.x <= mx && mx <= copy_end.x &&
            copy_start.y <= my && my <= copy_end.y) {
            copied_button_pressed = true;
            ui_config::last_copied = std::chrono::steady_clock::now();
            ui_config::copied = true;
        }
    }

    if (!center_button_pressed && !copied_button_pressed) {
        int selected_body_i = -2;
        if (ui_config::view_3d) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                selected_body_i = pick_body_at_mouse_3d(snap, make_camera());
            }
        }
        else {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                selected_body_i = pick_body_at_mouse_2d(snap);
            }
        }

        if (selected_body_i != -2 && selected_body_i != config::planet_info_display_index) {
            config::republish_needed = true;
            config::planet_info_display_index = selected_body_i;
        }
    }

    if (center_button_pressed) {
        const int center_index = config::planet_info_display_index.load();
        config::center_celestial_body_index.store(center_index);
        fit_camera_to_planetary_system(snap, center_index);
        config::republish_needed = true;
        config::planet_info_display_index = -1; // optional: reset config::planet_info_display_index
    } else if (new_center_i != -1 && new_center_i != config::center_celestial_body_index) {
        if (snap->bodies[new_center_i].enabled) {
            config::center_celestial_body_index = new_center_i;
            fit_camera_to_planetary_system(snap, new_center_i);
            config::republish_needed = true;
        }
    }

    if (copied_button_pressed) {
        const auto& body = snap->detailed_body_display;
        if (body.has_value()) {
            const str text = std::format("{}\nYears simulated: {:.2f}\nPosition: {} m\nVelocity: {} m/s\nMass: {} kg", body->name, simulated_years(), body->position.to_string(), body->velocity.to_string(), body->mass);
            SetClipboardText(text.c_str());
        }
    }

    if (IsKeyPressed(KEY_R)) {
        config::SCALING = config::ORIGINAL_SCALING;
        config::AXIS_SCALING = config::ORIGINAL_AXIS_SCALING;
    }

    if (IsKeyPressed(KEY_SPACE)) {
        if (config::timer.is_running()) {
            config::timer.pause();
            config::republish_needed = true;
        } else {
            if (runtime_config::target_total_simulation_time < 0.0 || runtime_config::target_total_simulation_time > simulated_years()) {
                config::timer.resume();
            }
        }

    }

    if (IsKeyPressed(KEY_T)) {
        ui_config::view_3d = !ui_config::view_3d;
        if (ui_config::view_3d) {
            fit_camera_to_planetary_system(snap, config::center_celestial_body_index.load());
        }
    }
    if (!ui_config::view_3d && IsKeyPressed(KEY_L)) ui_config::view_legend = !ui_config::view_legend;
    if (IsKeyPressed(KEY_O)) ui_config::draw_orbits = !ui_config::draw_orbits;
    if (IsKeyPressed(KEY_M)) ui_config::draw_moon_orbits = !ui_config::draw_moon_orbits;

    const float scroll = GetMouseWheelMove();

    if (ui_config::view_3d) {
        bool cam_turned = false;

        const float drag_summand = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? (config::DRAG_SENSITIVITY * 5) : config::DRAG_SENSITIVITY * 2;

        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) {
            ui_config::cam_azimuth += drag_summand;
            cam_turned = true;
        }
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) {
            ui_config::cam_elevation += drag_summand;
            cam_turned = true;
        }
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
            ui_config::cam_azimuth -= drag_summand;
            cam_turned = true;
        }
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) {
            ui_config::cam_elevation -= drag_summand;
            cam_turned = true;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            auto [x, y] = GetMouseDelta();
            ui_config::cam_azimuth   += x * config::DRAG_SENSITIVITY; // left/right = spin
            ui_config::cam_elevation += y * config::DRAG_SENSITIVITY; // up/down = tilt
            cam_turned = true;
        }

        if (cam_turned) {
            // Clamp
            constexpr float lim = 89.0f * DEG2RAD;
            ui_config::cam_elevation = std::clamp(ui_config::cam_elevation, -lim, lim);
        }

        const float zoom_factor = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? (config::ZOOM_3D_FACTOR*config::ZOOM_3D_FACTOR) : config::ZOOM_3D_FACTOR;

        if (scroll > 0 || IsKeyDown(KEY_RIGHT_BRACKET)) { // "+" on QWERTZ
            ui_config::cam_distance *= 1/zoom_factor;
        } else if (scroll < 0 || IsKeyDown(KEY_SLASH)) { // "-" on QWERTZ
            ui_config::cam_distance *= zoom_factor;
        }

        ui_config::cam_distance = std::clamp(ui_config::cam_distance, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
        if (IsKeyPressed(KEY_R)) {
            // reset azimuth/elevation/distance to defaults
            ui_config::cam_azimuth = config::DEFAULT_CAM_AZIMUTH;
            ui_config::cam_elevation = config::DEFAULT_CAM_ELEVATION;
            ui_config::cam_distance = config::DEFAULT_CAM_DISTANCE;
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
    ClearBackground(ui_config::view_3d ? BLACK : WHITE);

    if (ui_config::view_3d) {
        draw_3d(snap);
    } else {
        draw_2d(snap);
    }


    EndDrawing();
}
