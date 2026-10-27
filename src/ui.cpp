#include "ui.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <iostream>
#include <mutex>
#include <raymath.h>
#include <rlgl.h>
#include <cfloat>
#include <imgui.h>
#include <rlImGui.h>

#include "screen_utils.hpp"
#include "simulation.hpp"

namespace ui_config {
    float cam_azimuth = config::DEFAULT_CAM_AZIMUTH; // 0
    
    float cam_elevation = config::DEFAULT_CAM_ELEVATION; // 35° shows 3D immediately
    float cam_distance = config::DEFAULT_CAM_DISTANCE; // radius in world units
    bool view_3d = false; // toggle with keybind
    bool view_legend = true; // toggle with keybind
    std::array<std::chrono::steady_clock::time_point, NUM_CELESTIAL_BODIES> last_copied{};
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

    [[nodiscard]] bool is_moon(const std::size_t body_index) {
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
        const std::size_t body_index,
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

        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            constexpr double DEPTH_MARGIN = 1.05;
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
        const std::size_t center_index
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
        for (const std::size_t moon_index : system->moon_indices) {
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

    // Seemingly "invalid" values like negative or non-finite masses are considered a "feature".
    bool imgui_input(ui::EditableDouble& edit, const double current_value, std::size_t i, const str& label, const str& id, int precision, const str& unit) {
        // Update continuously unless the user is editing
        if (!edit.editing) {
            edit.val = current_value;
            // TODO: Add pending state (because maybe the simulation doesn't process the command INSTANTLY)
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(std::format("{}:", label).c_str());
        ImGui::SameLine();

        ImGui::SetNextItemWidth(125.0f);
        const bool committed = ImGui::InputDouble(std::format("##{}_{}", id, i).c_str(), &edit.val, 0, 0, std::format("%.{}e", precision).c_str(), ImGuiInputTextFlags_EnterReturnsTrue);

        edit.editing = ImGui::IsItemActive();

        ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextUnformatted(unit.c_str());

        return committed;
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

void ui::draw_imgui_text(const std::string& text, const Vec2 pos, const float font_size, const Color color) {
    ImGui::GetBackgroundDrawList()->AddText(
        config::imgui_ui_font,
        font_size,
        ImVec2(static_cast<float>(std::round(pos.x)), static_cast<float>(std::round(pos.y))),
        to_imgui_color(color),
        text.c_str()
    );
}

void ui::draw_imgui_centered_text(
    const str& text,
    const Vec2 center,
    const float angle,
    const float font_size,
    const Color color
) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    const ImVec2 size = config::imgui_ui_font->CalcTextSizeA(
        font_size, FLT_MAX, 0.0f, text.c_str()
    );

    const ImVec2 target(
        static_cast<float>(center.x),
        static_cast<float>(center.y)
    );

    // For rotated text, draw at a safe location first to avoid clipping glyphs before their vertices are rotated.
    const ImVec2 pos = angle == 0.0f
        ? ImVec2(target.x - size.x / 2, target.y - size.y / 2)
        : ImVec2(
            ImGui::GetMainViewport()->Pos.x + 20.0f,
            ImGui::GetMainViewport()->Pos.y + 20.0f
        );

    const int start = draw_list->VtxBuffer.Size;

    draw_list->AddText(
        config::imgui_ui_font,
        font_size,
        pos,
        to_imgui_color(color),
        text.c_str()
    );

    if (angle == 0.0f) return;

    const float radians = angle * DEG2RAD;
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    const ImVec2 origin(
        pos.x + size.x / 2,
        pos.y + size.y / 2
    );

    // Rotate only the vertices we just created
    for (int i = start; i < draw_list->VtxBuffer.Size; i++) {
        ImVec2& p = draw_list->VtxBuffer[i].pos;

        const float x = p.x - origin.x;
        const float y = p.y - origin.y;

        p.x = target.x + x * c - y * s;
        p.y = target.y + x * s + y * c;
    }
}

void ui::draw_imgui_outlined_text(
    const str& text,
    const Vector2& pos,
    const float font_size,
    const Color& fill,
    const Color& outline
) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;

            draw_list->AddText(
                config::imgui_ui_font,
                font_size,
                ImVec2(pos.x + static_cast<float>(dx), pos.y + static_cast<float>(dy)),
                to_imgui_color(outline),
                text.c_str()
            );
        }
    }

    draw_list->AddText(
        config::imgui_ui_font,
        font_size,
        ImVec2(pos.x, pos.y),
        to_imgui_color(fill),
        text.c_str()
    );
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
            draw_imgui_centered_text(
                round_to_hundreds((static_cast<double>(x) / config::GRID_SPACING - static_cast<double>(vertical_lines) / 2 - 0.5) * config::AXIS_SCALING / (static_cast<float>(vertical_lines) / 2)),
                {static_cast<double>(x), static_cast<double>(config::WINDOW_HEIGHT - config::WINDOW_MARGIN + 25)},
                0,
                20,
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
            draw_imgui_centered_text(
                round_to_hundreds((static_cast<double>(y) / config::GRID_SPACING - static_cast<double>(horizontal_lines) / 2 - 0.5) * config::AXIS_SCALING / (static_cast<float>(horizontal_lines) / 2)),
                {static_cast<double>(config::WINDOW_MARGIN - 25), static_cast<double>(y)},
                0,
                20,
                BLACK
            );
        }
    }

    draw_imgui_centered_text(
        std::format("Y Position ({} m)", SCALING_STRING),
        {static_cast<double>(config::WINDOW_MARGIN) / 2 - 20, static_cast<double>(config::WINDOW_HEIGHT) / 2},
        270,
        24,
        BLACK
    );

    draw_imgui_centered_text(
        std::format("X Position ({} m)", SCALING_STRING),
        {static_cast<double>(config::WINDOW_WIDTH) / 2, config::WINDOW_HEIGHT - static_cast<double>(config::WINDOW_MARGIN) / 2 + 20},
        0,
        24,
        BLACK
    );
}

void ui::draw_legend(const std::shared_ptr<const RenderSnapshot>& snap) {
    constexpr float font_size = 16;
    constexpr float spacing = font_size + 2;
    constexpr float padding = 8.0f;

    const Vec2 start = {config::WINDOW_WIDTH - config::WINDOW_MARGIN * 1.25 - config::GRID_SPACING, config::WINDOW_MARGIN};

    int visible_count = 0;

    for (const auto& body : snap->bodies) {
        if (body.color.has_value()) visible_count++;
    }

    const Vec2 end = {config::WINDOW_WIDTH - config::WINDOW_MARGIN, start.y + static_cast<float>(visible_count) * spacing + 2 * padding};

    draw_imgui_rectangle(start, end, WHITE);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 p_min(
            static_cast<float>(start.x),
            static_cast<float>(start.y)
        );
    const ImVec2 p_max(
        static_cast<float>(end.x),
        static_cast<float>(end.y)
    );

    // Background
    dl->AddRectFilled(p_min, p_max, to_imgui_color(WHITE));

    // Border
    dl->AddRect(p_min, p_max, to_imgui_color(BLACK));

    int row = 0;

    for (const auto& body : snap->bodies) {
        if (!body.color.has_value())
            continue;

        const auto y = static_cast<float>(
            start.y + padding + static_cast<float>(row) * spacing
        );

        draw_imgui_circle({start.x + 12, y + font_size / 2}, 5.0f, *body.color);

        draw_imgui_text(body.name, {start.x + 25, y}, font_size, BLACK);

        row++;
    }
}

void ui::imgui_style_setup(const bool dark_mode) {
    const ImU32 text_color = dark_mode ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 255);
    const ImU32 background_color = dark_mode ? IM_COL32(20, 20, 20, 255) : IM_COL32(245, 245, 245, 255);
    const ImU32 button_color = dark_mode ? IM_COL32(40, 40, 40, 255) : IM_COL32(220, 220, 220, 255);
    const ImU32 button_hovered_color = dark_mode ? IM_COL32(60, 60, 60, 255) : IM_COL32(200, 200, 200, 255);
    const ImU32 button_active_color = dark_mode ? IM_COL32(80, 80, 80, 255) : IM_COL32(180, 180, 180, 255);

    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background_color);

    ImGui::PushStyleColor(ImGuiCol_TitleBg, button_color);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, background_color);

    ImGui::PushStyleColor(ImGuiCol_Button, button_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, button_color);

    ImGui::PushFont(config::imgui_ui_font);
}

void ui::imgui_style_cleanup() {
    ImGui::PopFont();
    ImGui::PopStyleColor(9);
}

void ui::draw_planet_info(const std::shared_ptr<const RenderSnapshot>& snap, const bool dark_mode) {
    imgui_style_setup(dark_mode);

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (config::is_planet_info_selected(i)) {
            const auto& body = snap->detailed_body_display[i];
            if (!body.has_value()) continue;

            bool highlighted = true;

            auto&[mass, px, py, pz, vx, vy, vz] = edit_states[i];

            if (!was_open[i]) {
                const float offset = static_cast<float>(next_window_slot++ % 8) * 35.0f;

                ImGui::SetNextWindowPos(
                    ImVec2(100.0f + offset, 100.0f + offset),
                    ImGuiCond_Appearing
                );

                was_open[i] = true;
            }

            const bool visible = ImGui::Begin(std::format("  {} ({}) ###planet_{}", body->name, i, i).c_str(), &highlighted, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

            // Draw even if collapsed

            if (body->color.has_value()) {
                const ImVec2 pos = ImGui::GetWindowPos();
                const ImGuiStyle& style = ImGui::GetStyle();

                const float title_height = ImGui::GetFrameHeight();

                const float x = pos.x + style.FramePadding.x + ImGui::GetFontSize() + style.ItemInnerSpacing.x + 7.0f;
                const float y = pos.y + title_height / 2.0f;

                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                // Allow drawing inside the title bar
                draw_list->PushClipRect(
                    pos,
                    ImVec2(pos.x + ImGui::GetWindowWidth(), pos.y + title_height),
                    false
                );

                draw_list->AddCircleFilled(
                    ImVec2(x, y),
                    6.0f,
                    to_imgui_color(*body->color)
                );

                draw_list->PopClipRect();
            }

            if (visible) {
                if (ImGui::BeginTable("PositionVelocity", 2)) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Position");
                    ImGui::Indent(15.0f);
                    if (imgui_input(px, body->position.x, i, "x", "x##position", 4, "m")) update_body_command_queue.push(UpdateBodyCommand{.i = i, .px = px.val});
                    if (imgui_input(py, body->position.y, i, "y", "y##position", 4, "m")) update_body_command_queue.push(UpdateBodyCommand{.i = i, .py = py.val});
                    if (imgui_input(pz, body->position.z, i, "z", "z##position", 4, "m")) update_body_command_queue.push(UpdateBodyCommand{.i = i, .pz = pz.val});
                    ImGui::Unindent(15.0f);

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Velocity");
                    ImGui::Indent(15.0f);

                    if (imgui_input(vx, body->velocity.x, i, "x", "x##velocity", 4, "m/s")) update_body_command_queue.push(UpdateBodyCommand{.i = i, .vx = vx.val});
                    if (imgui_input(vy, body->velocity.y, i, "y", "y##velocity", 4, "m/s")) update_body_command_queue.push(UpdateBodyCommand{.i = i, .vy = vy.val});
                    if (imgui_input(vz, body->velocity.z, i, "z", "z##velocity", 4, "m/s")) update_body_command_queue.push(UpdateBodyCommand{.i = i, .vz = vz.val});
                    ImGui::Unindent(15.0f);

                    ImGui::EndTable();
                }

                ImGui::Spacing();

                if (imgui_input(mass, body->mass, i, "Mass", "mass", 6, "kg")) update_body_command_queue.push(UpdateBodyCommand{.i = i, .mass = mass.val});

                // TODO: 5. Validate numeric inputs -> Negate negative masses?
                // TODO: Also show (and let edit?) relative position & velocity values to another (separate user-selected) body?

                ImGui::Spacing();

                if (ImGui::Button("Use as center")) {
                    config::center_celestial_body_index.store(i);
                    fit_camera_to_planetary_system(snap, i);
                    config::republish_needed = true;
                }

                ImGui::SameLine();

                const bool recently_copied = std::chrono::steady_clock::now() - ui_config::last_copied[i] < std::chrono::seconds(1);

                if (ImGui::Button(recently_copied ? "Copied!###copy_info" : "Copy info###copy_info")) {
                    const str text = std::format("{}\nYears simulated: {:.2f}\nPosition: {} m\nVelocity: {} m/s\nMass: {:e} kg", body->name, simulated_years(), body->position.to_string(), body->velocity.to_string(), body->mass);
                    SetClipboardText(text.c_str());
                    ui_config::last_copied[i] = std::chrono::steady_clock::now();
                }
            }

            ImGui::End();

            if (!highlighted) {
                config::unhighlight_planet_info(i);
                was_open[i] = false;
            }
        } else {
            was_open[i] = false;
        }
    }

    if (std::ranges::none_of(was_open, [](const bool open) { return open; })) {
        next_window_slot = 0;
    }

    imgui_style_cleanup();
}


void ui::draw_stats(const std::shared_ptr<const RenderSnapshot>& snap, const bool dark_mode) {
    const double seconds = config::timer.seconds();
    const double years = simulated_years();
    const double speed = seconds > 0.0 ? years / seconds : 0.0;

    const std::size_t center_i = config::center_celestial_body_index.load();
    const str center_name = center_i < snap->bodies.size() ? snap->bodies[center_i].name : "Unknown";
    const str target_speed = runtime_config::target_simulation_speed > 0.0 ? std::format("{:.2f} yr/s", runtime_config::target_simulation_speed) : "Unlimited";

    const bool running = config::timer.is_running();
    const bool can_resume = runtime_config::target_total_simulation_time < 0.0 || runtime_config::target_total_simulation_time > simulated_years();

    imgui_style_setup(dark_mode);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_TextDisabled, dark_mode ? IM_COL32(170, 170, 170, 255) : IM_COL32(100, 100, 100, 255));

    ImGui::SetNextWindowPos(ImVec2(config::WINDOW_MARGIN, 10.0f), ImGuiCond_Always);
    constexpr float toolbar_height = config::WINDOW_MARGIN - 16.0f;
    ImGui::SetNextWindowSize(ImVec2(config::WINDOW_WIDTH - 2.0f * config::WINDOW_MARGIN, toolbar_height), ImGuiCond_Always);

    if (ImGui::Begin("##SimulationStats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        // Center text within the current table column
        auto centered_text = [](const char* text, const bool disabled) {
            const float available_width = ImGui::GetContentRegionAvail().x;
            const float text_width = ImGui::CalcTextSize(text).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                std::max(0.0f, (available_width - text_width) * 0.5f)
            );

            if (disabled) {
                ImGui::TextDisabled("%s", text);
            } else {
                ImGui::TextUnformatted(text);
            }
        };

        // Small label above normal-sized value
        auto stat = [&](const char* label, const str& value) {
            ImGui::PushFont(config::imgui_ui_font_small);
            centered_text(label, true);
            ImGui::PopFont();

            centered_text(value.c_str(), false);
        };

        // Leave space for the Pause/Resume button.
        constexpr float button_area_width = 115.0f;

        const float table_width = std::max(
            1.0f,
            ImGui::GetContentRegionAvail().x - button_area_width
        );

        if (ImGui::BeginTable("##StatsTable", 3, ImGuiTableFlags_SizingStretchSame, ImVec2(table_width, 0.0f))) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, toolbar_height / 2);

            ImGui::TableSetColumnIndex(0);
            stat("TIME", std::format("{:.2f} yr", years));

            ImGui::TableSetColumnIndex(1);
            stat("SPEED", std::format("{:.2f} yr/s", speed));

            ImGui::TableSetColumnIndex(2);
            stat("STEP", runtime_config::time_step_string);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, toolbar_height / 2);

            ImGui::TableSetColumnIndex(0);
            stat("COMPUTE", std::format("{:.2f} s", seconds));

            ImGui::TableSetColumnIndex(1);
            stat("TARGET", target_speed);

            ImGui::TableSetColumnIndex(2);
            stat("CENTER", center_name);

            ImGui::EndTable();
        }

        constexpr float button_width = 32;
        constexpr float button_height = 32;

        const float button_x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - button_width * 1.5f;
        const float button_y = ImGui::GetWindowPos().y + (toolbar_height - button_height) * 0.5f;

        ImGui::SetCursorScreenPos(ImVec2(button_x, button_y));

        ImGui::BeginDisabled(!running && !can_resume);

        const bool clicked = ImGui::InvisibleButton(
            "##pause_resume",
            ImVec2(button_width, button_height)
        );

        ImGui::EndDisabled();

        if (clicked) {
            if (running) {
                config::timer.pause();
                config::republish_needed = true;
            } else {
                config::timer.resume();
            }
        }

        const ImVec2 button_start = ImGui::GetItemRectMin();
        const ImVec2 button_end = ImGui::GetItemRectMax();

        const ImVec2 center = {
            (button_start.x + button_end.x) * 0.5f,
            (button_start.y + button_end.y) * 0.5f
        };

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        if (running) {
            draw_list->AddRectFilled(
                {center.x - 6, center.y - 9},
                {center.x - 1, center.y + 9},
                to_imgui_color(BLACK)
            );

            draw_list->AddRectFilled(
                {center.x + 9, center.y - 9},
                {center.x + 3, center.y + 9},
                to_imgui_color(BLACK)
            );
        } else {
            draw_list->AddTriangleFilled(
                {center.x - 6, center.y - 9},
                {center.x + 9, center.y},
                {center.x - 6, center.y + 9},
                to_imgui_color(BLACK)
            );
        }
    }

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    imgui_style_cleanup();
}

void ui::draw_planets(const std::shared_ptr<const RenderSnapshot>& snap) {
    const std::optional<std::size_t> hovered_body_i = pick_body_at_mouse_2d(snap);

    // snap->bodies positions are already relative to the center body (see publish_snapshot)
    for (std::size_t i = 0; i < snap->bodies.size(); i++) {
        const auto& [name, position, radius_2d, _radius_3d, color, _planet_visual, _enabled] = snap->bodies[i];
        if (color.has_value() && inside_screen(position)) {
            const Vector2 pos = to_raylib(position);
            DrawCircleV(pos, radius_2d, *color);
            Vector2 text_pos = pos;
            text_pos.y -= 10;
            text_pos.x += 10;
            if (hovered_body_i.has_value() && i == *hovered_body_i) {
                draw_imgui_outlined_text(name, text_pos, 20, *color, {0, 0, 0, 125});
            }
        }
    }
}

void ui::draw_orbits(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (!ui_config::draw_orbits) return;

    const std::size_t orbit_max_i = ui_config::draw_moon_orbits ? NUM_CELESTIAL_BODIES : NUM_PLANETS + NUM_DWARF_PLANETS;
    for (std::size_t i = 0; i < orbit_max_i; i++) {
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

    const std::size_t orbit_max_i = ui_config::draw_moon_orbits ? NUM_CELESTIAL_BODIES : NUM_PLANETS + NUM_DWARF_PLANETS;
    for (std::size_t i = 0; i < orbit_max_i; i++) {
        const auto& [pts, color] = snap->orbits[i];
        if (!color || pts.size() < 2) continue;

        const auto segment_count = static_cast<float>(pts.size() - 1);

        // GetWorldToScreen doesn't clip points behind the camera
        // it can project them to a wrong on-screen spot, so skip those segments
        const Vector3 first_world = to_world(pts[0]);
        bool prev_in_front = Vector3DotProduct(Vector3Subtract(first_world, cam.position), forward) > 0;
        Vector2 prev_screen = prev_in_front ? GetWorldToScreen(first_world, cam) : Vector2{};

        for (std::size_t j = 1; j < pts.size(); j++) {
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
    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
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
    auto label_axis = [&](auto anchor_of) {
        for (int k = -8; k <= 8; k++) {
            if (k == 0) continue;
            const Vector3 w = anchor_of(k);

            // don't draw labels behind the camera
            if (Vector3DotProduct(Vector3Subtract(w, cam.position), forward) <= 0) continue;

            auto [x, y] = GetWorldToScreen(w, cam);
            str text = to_power_of10(k * config::WORLD_UNIT_METERS);
            draw_imgui_text(text, {x, y}, 14, LIGHTGRAY);
        }
    };

    label_axis([](const int k){return Vector3{static_cast<float>(k), 0, 0};}); // x
    label_axis([](const int k){return Vector3{0, 0, static_cast<float>(k)};}); // physics y -> raylib z
    label_axis([](const int k){return Vector3{0, static_cast<float>(k), 0};}); // raylib z -> raylib y
}


std::optional<std::size_t> ui::pick_body_at_mouse_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (!snap) return std::nullopt;

    const auto [mx, my] = GetMousePosition();

    std::optional<std::size_t> best;
    float best_dist_sq = 0;

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        constexpr float MIN_CLICK_RADIUS = 8.0f; // keeps tiny planets clickable

        const auto& body = snap->bodies[i];
        if (!body.color.has_value() || !inside_screen(body.position)) continue;

        const auto [sx, sy] = to_raylib(body.position);
        const float dx = mx - sx;
        const float dy = my - sy;
        const float dist_sq = dx * dx + dy * dy;

        const float hit_radius = std::max(body.radius_2d, MIN_CLICK_RADIUS);
        if (dist_sq <= hit_radius * hit_radius && (!best.has_value() || dist_sq < best_dist_sq)) {
            best = i;
            best_dist_sq = dist_sq;
        }
    }

    return best;
}

std::optional<std::size_t> ui::pick_body_at_mouse_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
    if (!snap) return std::nullopt;

    const auto [mx, my]   = GetMousePosition();
    const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

    std::optional<std::size_t> best;
    float best_cam_dist = 0; // front-most body under the cursor wins

    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
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
        if (!best.has_value() || cam_dist < best_cam_dist) {
            best = i;
            best_cam_dist = cam_dist;
        }
    }

    return best;
}

void ui::draw_planet_labels_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
    const std::optional<std::size_t> hovered_body_i = pick_body_at_mouse_3d(snap, cam);

    if (!hovered_body_i.has_value()) return;

    const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

    const auto& [name, pos, _radius_2d, _radius_3d, color, _planet_visual, _enabled] = snap->bodies[*hovered_body_i];
    if (!color.has_value()) return;

    const Vector3 world = to_world(pos);
    // GetWorldToScreen doesn't clip points behind the camera
    // it can project them to a wrong on-screen spot, so skip those segments
    if (Vector3DotProduct(Vector3Subtract(world, cam.position), forward) <= 0) return;

    Vector2 text_pos = GetWorldToScreen(world, cam);
    text_pos.y -= 10;
    text_pos.x += 10;
    draw_imgui_outlined_text(name, text_pos, 20, *color, {0, 0, 0, 125});
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
        draw_stats(snap, true);
        draw_planet_info(snap, true);
    }
}


void ui::draw_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (snap) {
        draw_orbits(snap);
        draw_planets(snap);
        draw_imgui_rectangle({0, 0}, {config::WINDOW_MARGIN, config::WINDOW_HEIGHT}, WHITE);
        draw_imgui_rectangle({0, 0}, {config::WINDOW_WIDTH, config::WINDOW_MARGIN}, WHITE);
        draw_imgui_rectangle({0, config::WINDOW_HEIGHT - config::WINDOW_MARGIN}, {config::WINDOW_WIDTH, config::WINDOW_HEIGHT}, WHITE);
        draw_imgui_rectangle({config::WINDOW_WIDTH - config::WINDOW_MARGIN, 0}, {config::WINDOW_WIDTH, config::WINDOW_HEIGHT}, WHITE);
        draw_stats(snap, false);
        draw_ui();
        if (ui_config::view_legend) draw_legend(snap);
        draw_planet_info(snap, false);
    }
}

void ui::UpdateDrawFrame() {
    std::shared_ptr<const RenderSnapshot> snap;
    {
        std::lock_guard lock(config::snapshot_lock);
        snap = config::latest_snapshot;
    }

    std::optional<std::size_t> new_center_i;

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_ZERO)) { new_center_i=0; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_ONE)) { new_center_i=1; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_TWO)) { new_center_i=2; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_THREE)) { new_center_i=3; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_FOUR)) { new_center_i=4; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_FIVE)) { new_center_i=5; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_SIX)) { new_center_i=6; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_SEVEN)) { new_center_i=7; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_EIGHT)) { new_center_i=8; }
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_NINE)) { new_center_i=9; }

    if (new_center_i.has_value()) {
        config::center_celestial_body_index.store(*new_center_i);
        fit_camera_to_planetary_system(snap, *new_center_i);
        config::republish_needed = true;
    }

    if (!ImGui::GetIO().WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        std::optional<std::size_t> selected_body_i;

        if (ui_config::view_3d) {
            selected_body_i = pick_body_at_mouse_3d(snap, make_camera());
        }
        else {
            selected_body_i = pick_body_at_mouse_2d(snap);
        }

        if (selected_body_i.has_value()) {
            if (config::is_planet_info_selected(*selected_body_i)) {
                config::unhighlight_planet_info(*selected_body_i);
                was_open[*selected_body_i] = false;
            } else {
                config::highlight_planet_info(*selected_body_i);
            }

            config::republish_needed = true;
        }
    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_R)) {
        config::SCALING = config::ORIGINAL_SCALING;
        config::AXIS_SCALING = config::ORIGINAL_AXIS_SCALING;
    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_SPACE)) {
        if (config::timer.is_running()) {
            config::timer.pause();
            config::republish_needed = true;
        } else {
            if (runtime_config::target_total_simulation_time < 0.0 || runtime_config::target_total_simulation_time > simulated_years()) {
                config::timer.resume();
            }
        }

    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_T)) {
        ui_config::view_3d = !ui_config::view_3d;
        if (ui_config::view_3d) {
            fit_camera_to_planetary_system(snap, config::center_celestial_body_index.load());
        }
    }
    if (!ui_config::view_3d && !ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_L)) ui_config::view_legend = !ui_config::view_legend;
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_O)) ui_config::draw_orbits = !ui_config::draw_orbits;
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_M)) ui_config::draw_moon_orbits = !ui_config::draw_moon_orbits;

    const float scroll = ImGui::GetIO().WantCaptureMouse ? 0.0f : GetMouseWheelMove();

    if (ui_config::view_3d) {
        bool cam_turned = false;

        if (!ImGui::GetIO().WantCaptureKeyboard) {
            const float drag_summand = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? config::DRAG_SENSITIVITY * 5 : config::DRAG_SENSITIVITY * 2;

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
        }

        if (!ImGui::GetIO().WantCaptureMouse && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
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

        if (!ImGui::GetIO().WantCaptureKeyboard) {
            const float zoom_factor = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? config::ZOOM_3D_FACTOR*config::ZOOM_3D_FACTOR : config::ZOOM_3D_FACTOR;

            if (scroll > 0 || IsKeyDown(KEY_RIGHT_BRACKET)) { // "+" on QWERTZ
                ui_config::cam_distance *= 1/zoom_factor;
            } else if (scroll < 0 || IsKeyDown(KEY_SLASH)) { // "-" on QWERTZ
                ui_config::cam_distance *= zoom_factor;
            }
        }

        ui_config::cam_distance = std::clamp(ui_config::cam_distance, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
        if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_R)) {
            // reset azimuth/elevation/distance to defaults
            ui_config::cam_azimuth = config::DEFAULT_CAM_AZIMUTH;
            ui_config::cam_elevation = config::DEFAULT_CAM_ELEVATION;
            ui_config::cam_distance = config::DEFAULT_CAM_DISTANCE;
        }
    } else {
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            const double zoom_factor = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? config::ZOOM_FACTOR*config::ZOOM_FACTOR : config::ZOOM_FACTOR;
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
    }

    BeginDrawing();
    rlImGuiBegin();

    ClearBackground(ui_config::view_3d ? BLACK : WHITE);

    if (ui_config::view_3d) {
        draw_3d(snap);
    } else {
        draw_2d(snap);
    }

    rlImGuiEnd();
    EndDrawing();
}
