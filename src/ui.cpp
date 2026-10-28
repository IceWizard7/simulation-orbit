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
    Vec2 view_offset_2d_m;
    Vec3 view_offset_3d_m;
    bool panning_2d = false;
    bool panning_3d = false;
    std::size_t followed_celestial_body_index = config::NO_BODY_SELECTED;

    float cam_azimuth = config::DEFAULT_CAM_AZIMUTH; // 0
    
    float cam_elevation = config::DEFAULT_CAM_ELEVATION; // 35° shows 3D immediately
    float cam_distance = config::DEFAULT_CAM_DISTANCE; // radius in world units
    bool view_3d = false; // default; toggle with keybind
    bool view_legend = true; // default; toggle with keybind
    std::array<std::chrono::steady_clock::time_point, NUM_CELESTIAL_BODIES> last_copied{};
    bool draw_moon_orbits = true; // default; toggle with keybind
    bool draw_orbits = true; // default; toggle with keybind

    enum class BodySizeMode {
        schematic, // fixed celestial body size (schematic, radius_2d)
        physical, // fixed celestial body size (physical, physical_radius_m)
        physical_with_markers // adaptive; std::max(schematic, physical)
    };

    auto body_size_mode = BodySizeMode::physical_with_markers;

    Vec2 get_2d_offset(const std::shared_ptr<const ui::RenderSnapshot>& snap) {
        if (!snap || followed_celestial_body_index == config::NO_BODY_SELECTED) return view_offset_2d_m;
        const auto& pos = snap->bodies[followed_celestial_body_index].position;
        return {pos.x + view_offset_2d_m.x, pos.y + view_offset_2d_m.y};
    }

    Vec3 get_3d_offset(const std::shared_ptr<const ui::RenderSnapshot>& snap) {
        if (!snap || followed_celestial_body_index == config::NO_BODY_SELECTED) return {};

        return snap->bodies[followed_celestial_body_index].position;
    }
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
        const auto physical_radius_world = static_cast<float>(body.physical_radius_m / config::WORLD_UNIT_METERS);

        if (ui_config::body_size_mode == ui_config::BodySizeMode::physical) {
            return physical_radius_world;
        }

        const Vector3 world_position = to_world(body.position);
        const float projected_radius = projected_radius_pixels(world_position, body.radius_3d, cam);

        if (!std::isfinite(projected_radius) || projected_radius <= 0.0f) {
            return ui_config::body_size_mode ==
                ui_config::BodySizeMode::physical_with_markers
                ? std::max(physical_radius_world, body.radius_3d)
                : body.radius_3d;
        }

        const float min_radius = is_moon(body_index) ? MOON_MIN_RADIUS_PX : PLANET_MIN_RADIUS_PX;
        const float max_radius = is_moon(body_index) ? MOON_MAX_RADIUS_PX : PLANET_MAX_RADIUS_PX;
        const float desired_radius = std::clamp(projected_radius, min_radius, max_radius);

        const float schematic_radius_world = body.radius_3d * desired_radius / projected_radius;

        if (ui_config::body_size_mode == ui_config::BodySizeMode::physical_with_markers) {
            return std::max(physical_radius_world, schematic_radius_world);
        }

        return schematic_radius_world;
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

    // Move camera to centered celestial body
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

    void draw_star_background_3d(const Camera3D& cam, const double far_plane) {
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

    // Accepted values:
    // - All finite negative & positive values (& 0)

    // Rejected values:
    // - Non-finite values (-inf, +inf, nan)
    bool imgui_input(ui::EditableDouble& edit, const double current_value, std::size_t i, const str& label, const str& id, int precision, const str& unit) {
        // Update continuously unless the user is editing
        if (!edit.editing) {
            edit.val = current_value;
            // A "pending" state is not necessary here, because
            // - either the UI jumps back to the old value (if the input was invalid)
            // - or the UI only shows the old value for at most ~1 frame, until a publish arrives and the UI updates
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

    double body_radius_pixels_2d(const ui::RenderSnapshot::Body& body) {
        if (ui_config::body_size_mode == ui_config::BodySizeMode::schematic) return body.radius_2d;

        // TODO: What if the screen isn't square?
        constexpr double viewport_width = config::WINDOW_WIDTH - 2.0 * config::WINDOW_MARGIN;
        const double pixels_per_meter = viewport_width / (2.0 * config::SCALING);

        switch (ui_config::body_size_mode) {
            case ui_config::BodySizeMode::physical:
                return body.physical_radius_m * pixels_per_meter;
            case ui_config::BodySizeMode::physical_with_markers:
                return std::max(
                    body.physical_radius_m * pixels_per_meter,
                    static_cast<double>(body.radius_2d)
                );
            default: return 0; // Never gets reached anyways
        }
    }

    void cancel_camera_drag() {
        ui_config::panning_2d = false;
        ui_config::panning_3d = false;
    }

    void reset_view_offsets() {
        ui_config::view_offset_2d_m = {};
        cancel_camera_drag();
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

void ui::draw_imgui_circle(const Vec2& pos, const float radius, const Color color) {
    ImGui::GetBackgroundDrawList()->AddCircleFilled(
        ImVec2(
            static_cast<float>(pos.x),
            static_cast<float>(pos.y)
        ),
        radius,
        to_imgui_color(color)
    );
}

void ui::draw_imgui_rectangle(const Vec2& a, const Vec2& b, const Color color) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(
            static_cast<float>(std::min(a.x, b.x)),
            static_cast<float>(std::min(a.y, b.y))
        ),
        ImVec2(
            static_cast<float>(std::max(a.x, b.x)),
            static_cast<float>(std::max(a.y, b.y))
        ),
        to_imgui_color(color)
    );
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

void ui::draw_ui_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    constexpr double viewport_width = config::WINDOW_WIDTH - 2.0 * config::WINDOW_MARGIN;
    constexpr double viewport_height = config::WINDOW_HEIGHT - 2.0 * config::WINDOW_MARGIN;

    // Distance in meters represented by one grid interval
    const double step_x = config::GRID_SPACING * (2.0 * config::SCALING) / viewport_width;
    const double step_y = config::GRID_SPACING * (2.0 * config::SCALING) / viewport_height;

    const double min_x = ui_config::get_2d_offset(snap).x - config::SCALING;
    const double max_x = ui_config::get_2d_offset(snap).x + config::SCALING;
    const double min_y = ui_config::get_2d_offset(snap).y - config::SCALING;
    const double max_y = ui_config::get_2d_offset(snap).y + config::SCALING;

    const double largest_coordinate = std::max({
        std::abs(min_x), std::abs(max_x),
        std::abs(min_y), std::abs(max_y)
    });

    const int axis_exponent = largest_coordinate > 0.0 ? // TODO Try removing check
        static_cast<int>(std::floor(std::log10(largest_coordinate)))
        : 0;

    const double axis_unit_m = std::pow(10.0, axis_exponent);
    const str scaling_string = to_power_of10(axis_unit_m);

    // first_k_(x | y) could only be summarized into 1 if
    // viewport_width == viewport_height and view_offset_m.x == view_offset_m.y always holds
    const auto first_k_x = static_cast<long long>(std::ceil(min_x / step_x));
    const auto last_k_x = static_cast<long long>(std::floor(max_x / step_x));
    const auto first_k_y = static_cast<long long>(std::ceil(min_y / step_y));
    const auto last_k_y = static_cast<long long>(std::floor(max_y / step_y));

    constexpr float thick = 1.5;
    const Color color = Fade(DARKGRAY, 0.35f);

    // Grid, axes & labels
    for (auto k = first_k_x; k <= last_k_x; k++) {
        const double world_x = static_cast<double>(k) * step_x;

        const auto screen_x = to_raylib(
            Vec3{world_x, 0.0, 0.0},
            ui_config::get_2d_offset(snap)
        ).x;

        DrawLine(
            Vec2{screen_x, config::WINDOW_MARGIN},
            Vec2{screen_x, config::WINDOW_HEIGHT - config::WINDOW_MARGIN},
            thick,
            color
        );

        const double label_value = world_x / axis_unit_m;

        // Keep labels away from the viewport corners
        if (screen_x > config::WINDOW_MARGIN + 20 &&
            screen_x < config::WINDOW_WIDTH - config::WINDOW_MARGIN - 20) {
            draw_imgui_centered_text(
                round_to_hundreds(label_value),
                Vec2{screen_x, config::WINDOW_HEIGHT - config::WINDOW_MARGIN + 25},
                0,
                20,
                BLACK
            );
        }
    }

    for (auto k = first_k_y; k <= last_k_y; k++) {
        const double world_y = static_cast<double>(k) * step_y;

        const auto screen_y = to_raylib(
            Vec3{0.0, world_y, 0.0},
            ui_config::get_2d_offset(snap)
        ).y;

        DrawLine(
            Vec2{config::WINDOW_MARGIN, screen_y},
            Vec2{config::WINDOW_WIDTH - config::WINDOW_MARGIN, screen_y},
            thick,
            color
        );

        const double label_value = world_y / axis_unit_m;

        // Keep labels away from the viewport corners
        if (screen_y > config::WINDOW_MARGIN + 20 &&
            screen_y < config::WINDOW_HEIGHT - config::WINDOW_MARGIN - 20) {
            draw_imgui_centered_text(
                round_to_hundreds(label_value),
                Vec2{config::WINDOW_MARGIN - 25, screen_y},
                0,
                20,
                BLACK
            );
        }
    }

    draw_imgui_centered_text(
        std::format("Y Position ({} m)", scaling_string),
        {static_cast<double>(config::WINDOW_MARGIN) / 2 - 20, static_cast<double>(config::WINDOW_HEIGHT) / 2},
        270,
        24,
        BLACK
    );

    draw_imgui_centered_text(
        std::format("X Position ({} m)", scaling_string),
        {static_cast<double>(config::WINDOW_WIDTH) / 2, config::WINDOW_HEIGHT - static_cast<double>(config::WINDOW_MARGIN) / 2 + 20},
        0,
        24,
        BLACK
    );
}

void ui::draw_viewport_borders_2d() {
    constexpr float thick = 2;
    constexpr auto color = BLACK;

    // Top
    DrawLine(
        Vec2{config::WINDOW_MARGIN, config::WINDOW_MARGIN},
        Vec2{config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN},
        thick,
        color
    );

    // Bottom
    DrawLine(
        Vec2{config::WINDOW_MARGIN, config::WINDOW_HEIGHT - config::WINDOW_MARGIN},
        Vec2{config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_HEIGHT - config::WINDOW_MARGIN},
        thick,
        color
    );

    // Left
    DrawLine(
        Vec2{config::WINDOW_MARGIN, config::WINDOW_MARGIN},
        Vec2{config::WINDOW_MARGIN, config::WINDOW_HEIGHT - config::WINDOW_MARGIN},
        thick,
        color
    );

    // Right
    DrawLine(
        Vec2{config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_MARGIN},
        Vec2{config::WINDOW_WIDTH - config::WINDOW_MARGIN, config::WINDOW_HEIGHT - config::WINDOW_MARGIN},
        thick,
        color
    );
}

void ui::draw_legend_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
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

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(
        static_cast<float>(start.x),
        static_cast<float>(start.y)
    ));

    ImGui::SetNextWindowSize(ImVec2(
        static_cast<float>(end.x - start.x),
        static_cast<float>(end.y - start.y)
    ));

    if (ImGui::Begin("##Legend", nullptr, flags)) {
        int row = 0;

        for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            const auto& body = snap->bodies[i];

            if (!body.color.has_value()) continue;

            const auto y = static_cast<float>(start.y + padding + static_cast<float>(row) * spacing);

            Vec2 center = {start.x + 12, y + font_size / 2};
            constexpr float radius = 5.0f;

            draw_imgui_circle(center, radius, *body.color);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const auto [mx, my] = ImGui::GetMousePos();

                const double dx = mx - center.x;
                const double dy = my - center.y;

                if (dx * dx + dy * dy <= radius * radius) {
                    if (body.rendering_visible) {
                        update_body_command_queue.push(UpdateBodyCommand{.i = i, .disable_rendering = true});
                    } else {
                        update_body_command_queue.push(UpdateBodyCommand{.i = i, .enable_rendering = true});
                    }

                }
            }

            // Only draw a ring; not a full circle
            if (!body.rendering_visible) {
                draw_imgui_circle(center, 3.5f, WHITE);
            }

            const Vec2 pos = {start.x + 25, y};

            draw_imgui_text(body.name, pos, font_size, BLACK);

            ImGui::SetCursorScreenPos(ImVec2(static_cast<float>(pos.x), static_cast<float>(pos.y)));

            if (ImGui::InvisibleButton(
                std::format("##planet_toggle_{}", i).c_str(),
                ImVec2(static_cast<float>(config::WINDOW_WIDTH - config::WINDOW_MARGIN - pos.x), font_size)
            )) {
                if (config::is_planet_info_selected(i)) {
                    config::unhighlight_planet_info(i);
                    was_open[i] = false;
                } else {
                    config::highlight_planet_info(i);
                }

                // Request new info for DetailedBody
                config::republish_needed = true;
            }

            row++;
        }
    }

    ImGui::End();
}

void ui::imgui_style_setup(const bool dark_mode) {
    const ImU32 text_color = dark_mode ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 255);
    const ImU32 background_color = dark_mode ? IM_COL32(20, 20, 20, 255) : IM_COL32(245, 245, 245, 255);
    const ImU32 button_color = dark_mode ? IM_COL32(40, 40, 40, 255) : IM_COL32(220, 220, 220, 255);
    const ImU32 button_hovered_color = dark_mode ? IM_COL32(60, 60, 60, 255) : IM_COL32(200, 200, 200, 255);
    const ImU32 button_active_color = dark_mode ? IM_COL32(80, 80, 80, 255) : IM_COL32(180, 180, 180, 255);

    // Text & Background
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background_color);

    // Title bar
    ImGui::PushStyleColor(ImGuiCol_TitleBg, button_color);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, background_color);

    // Buttons
    ImGui::PushStyleColor(ImGuiCol_Button, button_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);

    // Background of many elements (ex. text input)
    ImGui::PushStyleColor(ImGuiCol_FrameBg, button_color);

    // Combo preview hover/active states
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, button_active_color);

    // Combo dropdown and tooltip background
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background_color);

    // Dropdown entries: selected, hovered, pressed
    ImGui::PushStyleColor(ImGuiCol_Header, button_color);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, button_active_color);

    // Popup/tooltip outline
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        dark_mode
            ? IM_COL32(90, 90, 90, 255)
            : IM_COL32(160, 160, 160, 255)
    );

    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

    // Font
    ImGui::PushFont(config::imgui_ui_font);
}

void ui::imgui_style_cleanup() {
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(16);
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

                // TODO: Also show (and let edit?) relative position & velocity values to another (separate user-selected) body?

                ImGui::Spacing();

                if (ImGui::Button("Use as center")) {
                    config::center_celestial_body_index.store(i);
                    fit_camera_to_planetary_system(snap, i);

                    ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
                    reset_view_offsets();

                    // New center
                    config::republish_needed = true;
                }

                ImGui::SameLine();

                const bool recently_copied = std::chrono::steady_clock::now() - ui_config::last_copied[i] < std::chrono::seconds(1);

                if (ImGui::Button(recently_copied ? "Copied!###copy_info" : "Copy info###copy_info")) {
                    const str text = std::format("{}\nYears simulated: {:.2f}\nPosition: {} m\nVelocity: {} m/s\nMass: {:e} kg", body->name, simulated_years(), body->position.to_string(), body->velocity.to_string(), body->mass);
                    SetClipboardText(text.c_str());
                    ui_config::last_copied[i] = std::chrono::steady_clock::now();
                }

                ImGui::SameLine();

                const ImGuiStyle& style = ImGui::GetStyle();

                const char* follow_label = ui_config::followed_celestial_body_index == i ? "Stop following" : "Follow";

                const float focus_width = ui_config::view_3d ? 0 : ImGui::CalcTextSize("Focus").x + 2.0f * style.FramePadding.x;
                const float follow_width = ImGui::CalcTextSize(follow_label).x + 2.0f * style.FramePadding.x;
                const float group_width = follow_width + (ui_config::view_3d ? 0.0f : focus_width + style.ItemSpacing.x);

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                ImGui::BeginGroup();

                if (!ui_config::view_3d) {
                    if (ImGui::Button("Focus")) {
                        ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
                        ui_config::view_offset_2d_m = {snap->bodies[i].position.x, snap->bodies[i].position.y};
                        cancel_camera_drag();
                    }

                    ImGui::SameLine();
                }

                if (ImGui::Button(std::format("{}###Follow_StopFollow", follow_label).c_str())) {
                    if (ui_config::followed_celestial_body_index == i) {
                        ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
                    } else {
                        ui_config::followed_celestial_body_index = i;
                    }

                    reset_view_offsets();
                }

                ImGui::EndGroup();
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
    const str center_name = center_i < snap->bodies.size() ? snap->bodies[center_i].name : "None";
    const str target_speed = runtime_config::target_simulation_speed > 0.0 ? std::format("{:.2f} yr/s", runtime_config::target_simulation_speed) : "Unlimited";

    const bool running = config::timer.is_running();
    const bool can_resume = runtime_config::target_total_simulation_time < 0.0 || runtime_config::target_total_simulation_time > simulated_years();

    imgui_style_setup(dark_mode);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_TextDisabled, dark_mode ? IM_COL32(170, 170, 170, 255) : IM_COL32(100, 100, 100, 255));

    constexpr float toolbar_height = config::WINDOW_MARGIN - 16.0f;
    constexpr float toolbar_width = config::WINDOW_WIDTH - 2.0f * config::WINDOW_MARGIN;

    ImGui::SetNextWindowPos(ImVec2(config::WINDOW_MARGIN, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbar_width, toolbar_height), ImGuiCond_Always);

    if (ImGui::Begin("##SimulationStats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
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

        // Leave space for the Pause/Resume button
        constexpr float buttons_width = 300.0f;
        const float gap = ImGui::GetStyle().ItemSpacing.x;

        const float table_width = std::max(1.0f, ImGui::GetContentRegionAvail().x - buttons_width - gap);

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

        ImGui::SameLine();

        if (ImGui::BeginTable("##ControlsTable", 3, ImGuiTableFlags_SizingStretchSame, ImVec2(buttons_width, 0.0f))) {
            ImGui::TableSetupColumn("Sizes", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_WidthFixed, 45.0f);
            ImGui::TableSetupColumn("Playback", ImGuiTableColumnFlags_WidthFixed, 26.0f);

            ImGui::TableNextRow();

            // Center each widget vertically within the toolbar window.
            auto center_vertically = [](const float height) {
                ImGui::SetCursorPosY(
                    (ImGui::GetWindowSize().y - height) * 0.5f
                );
            };

            // Body sizes
            ImGui::TableSetColumnIndex(0);
            center_vertically(ImGui::GetFrameHeight());

            constexpr const char* size_modes[] = {
                "Schematic",
                "Physical",
                "Physical + markers"
            };

            int selected_mode = static_cast<int>(ui_config::body_size_mode);

            ImGui::SetNextItemWidth(-1.0f); // Fill this column
            if (ImGui::Combo(
                "##body_sizes", &selected_mode,
                size_modes, IM_ARRAYSIZE(size_modes)
            )) {
                ui_config::body_size_mode =
                    static_cast<ui_config::BodySizeMode>(selected_mode);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Body sizes");
            }

            // 2D / 3D
            ImGui::TableSetColumnIndex(1);
            center_vertically(ImGui::GetFrameHeight());

            ImGui::TableSetColumnIndex(1);
            center_vertically(ImGui::GetFrameHeight());

            if (ImGui::Button(
                ui_config::view_3d ? "3D" : "2D",
                ImVec2(-1.0f, 0.0f)
            )) {
                ui_config::view_3d = !ui_config::view_3d;
                cancel_camera_drag();

                const auto center = config::center_celestial_body_index.load();
                if (ui_config::view_3d && center != config::NO_BODY_SELECTED) {
                    fit_camera_to_planetary_system(snap, center);
                }
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Switch between 2D and 3D");
            }

            // Pause / resume
            ImGui::TableSetColumnIndex(2);
            center_vertically(ImGui::GetFrameHeight());

            constexpr float button_width = 18.0f;
            constexpr float button_height = 18.0f;

            const float available_width = ImGui::GetContentRegionAvail().x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX()
                + std::max(0.0f, (available_width - button_width) * 0.5f)
            );

            ImGui::BeginDisabled(!running && !can_resume);

            const bool clicked = ImGui::InvisibleButton(
                "##pause_resume",
                ImVec2(button_width, button_height)
            );

            ImGui::EndDisabled();

            if (clicked) {
                if (running) {
                    config::timer.pause();

                    // Last publish, so the UI is up-to-date
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

            const ImU32 color = dark_mode ? IM_COL32(170, 170, 170, 255) : IM_COL32(100, 100, 100, 255);

            if (running) {
                // Pause symbol
                draw_list->AddRectFilled(
                    {center.x - 6, center.y - 9},
                    {center.x - 0.5f, center.y + 9},
                    color
                );

                draw_list->AddRectFilled(
                    {center.x + 3.5f, center.y - 9},
                    {center.x + 9, center.y + 9},
                    color
                );
            } else {
                // Resume symbol
                draw_list->AddTriangleFilled(
                    {center.x - 6, center.y - 9},
                    {center.x + 9, center.y},
                    {center.x - 6, center.y + 9},
                    color
                );
            }

            ImGui::EndTable();
        }
    }

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    imgui_style_cleanup();
}

void ui::draw_planets_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    const std::optional<std::size_t> hovered_body_i = pick_body_at_mouse_2d(snap);

    // snap->bodies positions are already relative to the center body (see publish_snapshot)
    for (std::size_t i = 0; i < snap->bodies.size(); i++) {
        const auto& [name, position, physical_radius_m, radius_2d, _radius_3d, color, _planet_visual, rendering_visible] = snap->bodies[i];

        if (!rendering_visible || !color.has_value()) continue;

        const double radius_px = body_radius_pixels_2d(snap->bodies[i]);
        const Vector2 pos = to_raylib(position, ui_config::get_2d_offset(snap));

        if (!circle_intersects_viewport(pos, radius_px)) continue;

        DrawCircleV(pos, static_cast<float>(radius_px), *color);

        const Vector2 text_pos = {pos.x - 10, pos.y - 10};
        if (hovered_body_i.has_value() && i == *hovered_body_i) {
            draw_imgui_outlined_text(name, text_pos, 20, *color, {0, 0, 0, 125});
        }
    }
}

// 2 simple inside_screen() checks aren't sufficient, example case:
// start is outside on the left, end is outside on the top, and the line connecting them is partially inside the viewport
// This is really visible when you zoom in very close into an orbit
bool ui::clip_segment_2d(Vec3& start, Vec3& end, const std::shared_ptr<const RenderSnapshot>& snap) {
    const Vec3 original_start = start;
    const Vec3 delta_vec = end - start;

    double t_enter = 0.0;
    double t_exit = 1.0;

    auto clip_axis = [&](const double origin, const double delta, const double minimum, const double maximum) {
        if (delta == 0.0) {
            // Parallel to these boundaries: entirely inside or outside.
            return origin >= minimum && origin <= maximum;
        }

        double t1 = (minimum - origin) / delta;
        double t2 = (maximum - origin) / delta;

        if (t1 > t2) std::swap(t1, t2);

        t_enter = std::max(t_enter, t1);
        t_exit = std::min(t_exit, t2);

        return t_enter <= t_exit;
    };

    if (!clip_axis(start.x, delta_vec.x, ui_config::get_2d_offset(snap).x - config::SCALING, ui_config::get_2d_offset(snap).x + config::SCALING) ||
        !clip_axis(start.y, delta_vec.y, ui_config::get_2d_offset(snap).y - config::SCALING, ui_config::get_2d_offset(snap).y + config::SCALING)) {
        return false;
    }

    start = original_start + delta_vec * t_enter;
    end = original_start + delta_vec * t_exit;

    return true;

}

void ui::draw_orbits_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (!ui_config::draw_orbits) return;

    const std::size_t orbit_max_i = ui_config::draw_moon_orbits ? NUM_CELESTIAL_BODIES : NUM_PLANETS + NUM_DWARF_PLANETS;
    for (std::size_t i = 0; i < orbit_max_i; i++) {
        const auto& [points, color] = snap->orbits[i];
        if (!color.has_value() || points.size() < 2 || !snap->bodies[i].rendering_visible) continue;

        const auto segment_count = static_cast<float>(points.size() - 1);

        for (std::size_t j = 1; j < points.size(); j++) {
            const float age = static_cast<float>(j) / segment_count;
            const float alpha = 0.10f + 0.70f * age;

            Vec3 start = points[j - 1];
            Vec3 end = points[j];

            // For smoother edges, it's now enough if only 1 point is inside (aggressive DrawRectangle deals with the rest)
            if (clip_segment_2d(start, end, snap)) {
                DrawLineEx(
                    to_raylib(start, ui_config::get_2d_offset(snap)),
                    to_raylib(end, ui_config::get_2d_offset(snap)),
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
        const auto& [points, color] = snap->orbits[i];
        if (!color.has_value() || points.size() < 2 || !snap->bodies[i].rendering_visible) continue;

        const auto segment_count = static_cast<float>(points.size() - 1);

        // GetWorldToScreen doesn't clip points behind the camera
        // it can project them to a wrong on-screen spot, so skip those segments
        const Vector3 first_world = to_world(points[0]);
        bool prev_in_front = Vector3DotProduct(Vector3Subtract(first_world, cam.position), forward) > 0;

        for (std::size_t j = 1; j < points.size(); j++) {
            const Vector3 world = to_world(points[j]);
            const bool in_front = Vector3DotProduct(Vector3Subtract(world, cam.position), forward) > 0;

            if (prev_in_front && in_front) {
                const float alpha = 0.10f + 0.70f * (static_cast<float>(j) / segment_count);

                DrawLine3D(
                    to_world(points[j - 1]),
                    to_world(points[j]),
                    Fade(*color, alpha)
                );
            }

            prev_in_front = in_front;
        }
    }
}

void ui::draw_planets_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const auto& [name, pos, _physical_radius_m, _radius_2d, _radius_3d, color, planet_visual, rendering_visible] = snap->bodies[i];

        if (!rendering_visible) continue;

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
        if (!body.rendering_visible) continue;

        const auto [sx, sy] = to_raylib(body.position, ui_config::get_2d_offset(snap));
        const float dx = mx - sx;
        const float dy = my - sy;
        const float dist_sq = dx * dx + dy * dy;

        const double hit_radius = std::max(body_radius_pixels_2d(body), static_cast<double>(MIN_CLICK_RADIUS));
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
        if (!body.rendering_visible) continue;

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

    const auto& [name, pos, _physical_radius_m, _radius_2d, _radius_3d, color, _planet_visual, rendering_visible] = snap->bodies[*hovered_body_i];
    if (!color.has_value() || !rendering_visible) return;

    const Vector3 world = to_world(pos);
    // GetWorldToScreen doesn't clip points behind the camera
    // it can project them to a wrong on-screen spot, so skip those segments
    if (Vector3DotProduct(Vector3Subtract(world, cam.position), forward) <= 0) return;

    Vector2 text_pos = GetWorldToScreen(world, cam);
    text_pos.y -= 10;
    text_pos.x += 10;
    draw_imgui_outlined_text(name, text_pos, 20, *color, {0, 0, 0, 125});
}

Camera3D ui::make_camera(const std::shared_ptr<const RenderSnapshot>& snap) {
    Camera3D c{};
    c.target = to_world(ui_config::get_3d_offset(snap));
    c.up = {0, 1, 0};
    c.fovy = CAMERA_FOVY;
    c.projection = CAMERA_PERSPECTIVE; // alternative: CAMERA_ORTHOGRAPHIC
    c.position = {
        ui_config::cam_distance * cosf(ui_config::cam_elevation) * cosf(ui_config::cam_azimuth),
        ui_config::cam_distance * sinf(ui_config::cam_elevation),
        ui_config::cam_distance * cosf(ui_config::cam_elevation) * sinf(ui_config::cam_azimuth)
    };

    c.position = Vector3Add(c.position, c.target);

    return c;
}

void ui::draw_3d(const std::shared_ptr<const RenderSnapshot>& snap) {
    const Camera3D cam = make_camera(snap);
    const double far_plane = camera_far_plane(cam, snap);
    rlSetClipPlanes(camera_near_plane(cam), far_plane);

    BeginMode3D(cam);
    draw_star_background_3d(cam, far_plane);
    EndMode3D();

    if (snap) {
        BeginMode3D(cam);
        draw_planets_3d(snap, cam);

        rlDrawRenderBatchActive(); // Finish opaque geometry first.
        rlDisableDepthMask(); // Trails test depth, but don't write it

        draw_orbits_3d(snap, cam);

        rlDrawRenderBatchActive(); // Submit trails before restoring state
        rlEnableDepthMask();
        EndMode3D();

        // 2D overlays, projected by 3D points
        draw_planet_labels_3d(snap, cam);
        draw_stats(snap, true);
        draw_planet_info(snap, true);
    }
}


void ui::draw_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (snap) {
        draw_orbits_2d(snap);
        draw_planets_2d(snap);

        draw_imgui_rectangle({0, 0}, {config::WINDOW_MARGIN, config::WINDOW_HEIGHT}, WHITE);
        draw_imgui_rectangle({0, 0}, {config::WINDOW_WIDTH, config::WINDOW_MARGIN}, WHITE);
        draw_imgui_rectangle({0, config::WINDOW_HEIGHT - config::WINDOW_MARGIN}, {config::WINDOW_WIDTH, config::WINDOW_HEIGHT}, WHITE);
        draw_imgui_rectangle({config::WINDOW_WIDTH - config::WINDOW_MARGIN, 0}, {config::WINDOW_WIDTH, config::WINDOW_HEIGHT}, WHITE);

        draw_stats(snap, false);
        draw_ui_2d(snap);
        draw_viewport_borders_2d();
        if (ui_config::view_legend) draw_legend_2d(snap);
        draw_planet_info(snap, false);
    }
}

void ui::UpdateDrawFrame() {
    std::shared_ptr<const RenderSnapshot> snap;
    {
        std::lock_guard lock(config::snapshot_lock);
        snap = config::latest_snapshot;
    }

    if (!ImGui::GetIO().WantCaptureKeyboard) {
        std::optional<std::size_t> new_center_i;

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
        if (IsKeyPressed(config::KEY_FOLLOW_NONE)) { new_center_i=config::NO_BODY_SELECTED;}

        // Don't allow bodies that are invisible in rendering
        if (new_center_i.has_value() && (*new_center_i == config::NO_BODY_SELECTED ||
            (snap && *new_center_i < snap->bodies.size() && snap->bodies[*new_center_i].rendering_visible))) {
            config::center_celestial_body_index.store(*new_center_i);

            if (*new_center_i != config::NO_BODY_SELECTED) {
                fit_camera_to_planetary_system(snap, *new_center_i);
            }

            ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
            reset_view_offsets();

            // New center
            config::republish_needed = true;
        }
    }

    // Cancel an existing gesture (3d drag)
    if (!ui_config::view_3d ||
        !IsWindowFocused() ||
        !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        ui_config::panning_3d = false;
    }

    // Start only on a fresh press outside ImGui (3d drag)
    if (ui_config::view_3d && IsWindowFocused() &&
        !ImGui::GetIO().WantCaptureMouse &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {

        ui_config::panning_3d = true;
    }

    if (!ImGui::GetIO().WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        std::optional<std::size_t> selected_body_i;

        if (ui_config::view_3d) {
            selected_body_i = pick_body_at_mouse_3d(snap, make_camera(snap));
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

            // Request new info for DetailedBody
            config::republish_needed = true;
        }
    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_RESET)) {
        config::SCALING = config::ORIGINAL_SCALING;
        config::AXIS_SCALING = config::ORIGINAL_AXIS_SCALING;

        ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
        reset_view_offsets();
    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_RESUME_AND_PAUSE)) {
        if (config::timer.is_running()) {
            config::timer.pause();

            // Last publish, so the UI is up-to-date
            config::republish_needed = true;
        } else {
            if (runtime_config::target_total_simulation_time < 0.0 || runtime_config::target_total_simulation_time > simulated_years()) {
                config::timer.resume();
            }
        }
    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_3D_2D_TOGGLE)) {
        ui_config::view_3d = !ui_config::view_3d;
        cancel_camera_drag();

        if (ui_config::view_3d && config::center_celestial_body_index.load() != config::NO_BODY_SELECTED) {
            fit_camera_to_planetary_system(snap, config::center_celestial_body_index.load());
        }
    }
    if (!ui_config::view_3d && !ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_LEGEND_TOGGLE)) ui_config::view_legend = !ui_config::view_legend;
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_ORBIT_TOGGLE)) ui_config::draw_orbits = !ui_config::draw_orbits;
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_MOON_ORBIT_RENDERING_TOGGLE)) ui_config::draw_moon_orbits = !ui_config::draw_moon_orbits;

    const float scroll = ImGui::GetIO().WantCaptureMouse ? 0.0f : GetMouseWheelMove();
    const auto [mx, my] = GetMousePosition();

    if (ui_config::panning_2d && (!IsWindowFocused() || !IsMouseButtonDown(MOUSE_BUTTON_LEFT) || ui_config::view_3d)) {
        ui_config::panning_2d = false;
    }

    if (ui_config::view_3d) {
        bool cam_turned = false;

        if (!ImGui::GetIO().WantCaptureKeyboard) {
            const float drag_summand = std::ranges::any_of(config::KEYS_SHIFT, IsKeyDown) ? config::DRAG_SENSITIVITY * 5 : config::DRAG_SENSITIVITY * 2;

            if (std::ranges::any_of(config::KEYS_DRAG_LEFT, IsKeyDown)) {
                ui_config::cam_azimuth += drag_summand;
                cam_turned = true;
            }
            if (std::ranges::any_of(config::KEYS_DRAG_UP, IsKeyDown)) {
                ui_config::cam_elevation += drag_summand;
                cam_turned = true;
            }
            if (std::ranges::any_of(config::KEYS_DRAG_RIGHT, IsKeyDown)) {
                ui_config::cam_azimuth -= drag_summand;
                cam_turned = true;
            }
            if (std::ranges::any_of(config::KEYS_DRAG_DOWN, IsKeyDown)) {
                ui_config::cam_elevation -= drag_summand;
                cam_turned = true;
            }
        }

        if (!ImGui::GetIO().WantCaptureMouse && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (ui_config::panning_3d) {
                const auto [x, y] = GetMouseDelta();

                ui_config::cam_azimuth += x * config::DRAG_SENSITIVITY; // left/right = spin
                ui_config::cam_elevation += y * config::DRAG_SENSITIVITY; // up/down = tilt
                cam_turned = true;
            }
        }

        if (cam_turned) {
            // Clamp
            constexpr float lim = 89.0f * DEG2RAD;
            ui_config::cam_elevation = std::clamp(ui_config::cam_elevation, -lim, lim);
        }

        const float zoom_factor = std::ranges::any_of(config::KEYS_SHIFT, IsKeyDown) ? config::ZOOM_3D_FACTOR*config::ZOOM_3D_FACTOR : config::ZOOM_3D_FACTOR;

        if (scroll > 0 || (IsKeyDown(config::KEY_ZOOM_IN) && !ImGui::GetIO().WantCaptureKeyboard)) {
            ui_config::cam_distance *= 1/zoom_factor;
        } else if (scroll < 0 || (IsKeyDown(config::KEY_ZOOM_OUT) && !ImGui::GetIO().WantCaptureKeyboard)) {
            ui_config::cam_distance *= zoom_factor;
        }

        ui_config::cam_distance = std::clamp(ui_config::cam_distance, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
        if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_RESET)) {
            // reset azimuth/elevation/distance to defaults
            ui_config::cam_azimuth = config::DEFAULT_CAM_AZIMUTH;
            ui_config::cam_elevation = config::DEFAULT_CAM_ELEVATION;
            ui_config::cam_distance = config::DEFAULT_CAM_DISTANCE;
        }
    } else {
        if (!ui_config::panning_2d && !ImGui::GetIO().WantCaptureMouse && // Currently not panning, ImGui doesn't consume mouse
            IsWindowFocused() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && // Window focused & button is pressed
            !pick_body_at_mouse_2d(snap).has_value() && // Drag doesn't start on a celestial body
            config::WINDOW_MARGIN <= mx && mx <= config::WINDOW_WIDTH - config::WINDOW_MARGIN && // Mouse is in viewport (x & y)
            config::WINDOW_MARGIN <= my && my <= config::WINDOW_HEIGHT - config::WINDOW_MARGIN) {
            ui_config::panning_2d = true;
        }

        if (ui_config::panning_2d) {
            auto [dx, dy] = GetMouseDelta();

            ui_config::view_offset_2d_m.x -= dx  * (2.0 * config::SCALING) / (config::WINDOW_WIDTH - 2.0 * config::WINDOW_MARGIN);
            ui_config::view_offset_2d_m.y -= dy  * (2.0 * config::SCALING) / (config::WINDOW_HEIGHT - 2.0 * config::WINDOW_MARGIN);
        }


        const bool zoom_in = scroll > 0 || (!ImGui::GetIO().WantCaptureKeyboard && IsKeyDown(config::KEY_ZOOM_IN));
        const bool zoom_out = scroll < 0 || (!ImGui::GetIO().WantCaptureKeyboard && IsKeyDown(config::KEY_ZOOM_OUT));

        if (zoom_in || zoom_out) {
            const bool anchor_at_mouse = config::WINDOW_MARGIN <= mx && mx <= config::WINDOW_WIDTH - config::WINDOW_MARGIN && config::WINDOW_MARGIN <= my && my <= config::WINDOW_HEIGHT - config::WINDOW_MARGIN;
            const Vec2 before = screen_to_meters({mx, my}, ui_config::get_2d_offset(snap));
            const double zoom_factor = std::ranges::any_of(config::KEYS_SHIFT, IsKeyDown) ? config::ZOOM_FACTOR*config::ZOOM_FACTOR : config::ZOOM_FACTOR;

            if (zoom_in) {
                // scrolled up (=> in)
                if (config::AXIS_SCALING / zoom_factor < 4.0/3) {
                    config::AXIS_SCALING *= 10;
                    config::AXIS_SCALING /= zoom_factor;
                    config::SCALING /= zoom_factor;
                } else {
                    config::AXIS_SCALING /= zoom_factor;
                    config::SCALING /= zoom_factor;
                }
            } else if (zoom_out) {
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

            if (anchor_at_mouse) {
                const Vec2 after = screen_to_meters({mx, my}, ui_config::get_2d_offset(snap));

                ui_config::view_offset_2d_m.x += before.x - after.x;
                ui_config::view_offset_2d_m.y += before.y - after.y;
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
