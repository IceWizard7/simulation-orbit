#pragma once

#include <array>
#include <cstddef>
#include <imgui.h>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <raylib.h>
#include <utility>
#include <vector>

#include "config.hpp"
#include "planet_visual.hpp"
#include "vectors.hpp"

namespace ui_data {
    // Immutable, render-ready view of the system
    // Producer: simulation thread, Consumer: render thread
    // Positions & orbit points: relative to the center body => render thread can re-project them under the current zoom every frame
    struct RenderSnapshot {
        struct Body {
            str name;
            Vec3 relative_position;
            Vec3 absolute_position;
            Vec3 velocity;
            double mass;
            double physical_radius_m = 0.0;
            float radius_2d = 0;
            float radius_3d = 0;
            std::optional<Color> color;
            const PlanetVisual* planet_visual = nullptr;
            bool rendering_visible = false;
        };
        struct Orbit {
            std::vector<Vec3> points;
            std::optional<Color> color;
        };
        std::array<std::optional<Body>,  NUM_CELESTIAL_BODIES> bodies;
        std::array<Orbit, NUM_CELESTIAL_BODIES> orbits;
        std::size_t max_orbit_points_used = 0;
    };

    struct UpdateBodyCommand {
        std::size_t i;
        std::optional<std::size_t> reference_index;

        std::optional<double> mass;
        std::optional<double> px, py, pz;
        std::optional<double> vx, vy, vz;
        bool disable_rendering = false;
        bool enable_rendering = false;
    };

    struct EditableDouble {
        double val = 0.0;
        bool editing = false;
        bool dirty = false; // contains an unsubmitted change
    };

    struct BodyEditState {
        EditableDouble mass;
        EditableDouble px, py, pz;
        EditableDouble vx, vy, vz;

        std::optional<std::size_t> reference_index;
    };

    inline std::array<BodyEditState, NUM_CELESTIAL_BODIES> edit_states{};
    inline std::array<bool, NUM_CELESTIAL_BODIES> was_open{};
    inline std::size_t next_window_slot = 0;

    struct UpdateBodyCommandQueue {
    private:
        std::queue<UpdateBodyCommand> queue;
        std::mutex mtx;

    public:
        void push(const UpdateBodyCommand &cmd) {
            std::lock_guard lock(mtx);
            queue.push(cmd);
        }

        std::optional<UpdateBodyCommand> try_pop() {
            std::lock_guard lock(mtx);

            if (queue.empty()) return std::nullopt;

            const auto cmd = queue.front();
            queue.pop();

            return cmd;
        }
    };

    inline UpdateBodyCommandQueue update_body_command_queue;
}

namespace ui_config {
    inline Vec2 view_offset_2d_m;
    inline Vec3 view_offset_3d_m;
    inline bool panning_2d = false;
    inline bool panning_3d = false;
    inline std::size_t followed_celestial_body_index = config::NO_BODY_SELECTED;

    inline float cam_azimuth = config::DEFAULT_CAM_AZIMUTH; // 0
    inline float cam_elevation = config::DEFAULT_CAM_ELEVATION; // 35° shows 3D immediately
    inline float cam_distance = config::DEFAULT_CAM_DISTANCE; // radius in world units

    inline bool view_3d = false; // default; toggle with keybind / UI
    inline bool view_debug = false; // default; toggle with keybind
    inline bool legend_collapsed = false; // default; toggle with keybind / UI
    inline bool legend_collapse_requested = false; // helper

    inline std::array<std::chrono::steady_clock::time_point, NUM_CELESTIAL_BODIES> last_copied{};

    inline bool draw_moon_orbits = true; // default; toggle with keybind / UI
    inline bool draw_orbits = true; // default; toggle with keybind / UI

    inline std::size_t trail_segments_reduced_2d = 0;
    inline std::size_t trail_segments_submitted_2d = 0;

    enum class BodySizeMode {
        schematic, // fixed celestial body size (schematic, radius_2d)
        physical, // fixed celestial body size (physical, physical_radius_m)
        physical_with_markers // adaptive; std::max(schematic, physical)
    };

    inline auto body_size_mode = BodySizeMode::physical_with_markers;

    inline float marker_scale = 1.0f;

    inline Vec2 get_2d_offset(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
        if (!snap || followed_celestial_body_index >= snap->bodies.size() || !snap->bodies[followed_celestial_body_index]) return view_offset_2d_m;
        const auto& pos = snap->bodies[followed_celestial_body_index]->relative_position;
        return {pos.x + view_offset_2d_m.x, pos.y + view_offset_2d_m.y};
    }

    inline Vec3 get_3d_offset(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
        if (!snap || followed_celestial_body_index >= snap->bodies.size() || !snap->bodies[followed_celestial_body_index]) return {};

        return snap->bodies[followed_celestial_body_index]->relative_position;
    }

    constexpr float CAMERA_FOVY = 45.0f;
    constexpr float MIN_CAM_DISTANCE = 1.0e-6f;
    constexpr float MAX_CAM_DISTANCE = 200.0f;

    // 3d schematic/marker radii PX
    constexpr float PLANET_MIN_RADIUS_PX_3D = 4.0f;
    constexpr float PLANET_MAX_RADIUS_PX_3D = 12.0f;
    constexpr float MOON_MIN_RADIUS_PX_3D = 3.0f;
    constexpr float MOON_MAX_RADIUS_PX_3D = 8.0f;

    // 2d schematic/marker radii always uses radius_2d

    // keeps tiny planets clickable
    constexpr float MIN_CLICK_RADIUS_PX_2D = 8.0f;
    constexpr float MIN_CLICK_RADIUS_PX_3D = 12.0f;

    inline Model star_model{};
    inline Texture2D star_texture{};
    inline bool star_background_loaded = false;
}

namespace ui_helpers {
    // Inline functions

    [[nodiscard]] inline bool is_moon(const std::size_t body_index) {
        return body_index >= NUM_PLANETS + NUM_DWARF_PLANETS;
    }

    [[nodiscard]] inline double simulated_years() {
        return static_cast<double>(config::steps_simulated.load()) * runtime_config::time_step / config::SECONDS_PER_YEAR;
    }

    [[nodiscard]] inline double measured_simulation_speed() {
        const double seconds = config::timer.seconds();
        return seconds > 0.0 ? simulated_years() / seconds : 0.0;
    }

    [[nodiscard]] inline ImU32 to_imgui_color(const Color color) {
        return IM_COL32(color.r, color.g, color.b, color.a);
    }

    [[nodiscard]] inline std::pair<Vec2, Vec2> center_button_coordinates() {
        return {{config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 10}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}};
    }

    [[nodiscard]] inline std::pair<Vec2, Vec2> copy_button_coordinates() {
        return {{config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 50}};
    }

    inline void DrawLine(const Vec2& start_pos, const Vec2& end_pos, const float thick, const Color color) {
        DrawLineEx(Vector2(static_cast<float>(start_pos.x), static_cast<float>(start_pos.y)), Vector2(static_cast<float>(end_pos.x), static_cast<float>(end_pos.y)), thick, color);
    }

    // ImGui helpers

    bool imgui_input(ui_data::EditableDouble& edit,double current_value, std::size_t i, const str& label, const str& id, int precision, const str& unit);

    bool begin_labeled_combo(const str &label, const str &id, const str &preview, float width, const std::optional<Color>& color);

    bool body_selectable(const str& name, bool selected, const std::optional<Color>& color);

    void draw_imgui_text(const std::string& text, Vec2 pos, float font_size, Color color);

    void draw_imgui_centered_text(const str& text, Vec2 center, float angle, float font_size, Color color);

    void draw_imgui_circle(const Vec2& pos, float radius, Color color);

    void draw_imgui_rectangle(const Vec2& a, const Vec2& b, Color color);

    void draw_imgui_outlined_text(const str& text, const Vector2& pos, float font_size, const Color& fill, const Color& outline);

    void imgui_style_setup(bool dark_mode);

    void imgui_style_cleanup();

    bool reset_icon_button(const str& id);

    // Windows

    void draw_debug_overlay(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    void draw_legend(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void draw_planet_info(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    void draw_toolbar(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, bool dark_mode);

    // Generic helpers

    void cancel_camera_drag();

    void reset_view_offsets();

    void reset_view();

    void set_coordinate_origin(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, std::size_t index);

    void load_star_background();

    void unload_star_background();

    // Shared 2d & 3d (needed for ui/ui.cpp)
    std::optional<std::size_t> pick_body_at_mouse_2d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);
    std::optional<std::size_t> pick_body_at_mouse_3d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const Camera3D& cam);
    void fit_camera_to_planetary_system_3d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, std::size_t center_index);
    Camera3D make_camera_3d();

    void draw_3d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    void draw_2d(const std::shared_ptr<const ui_data::RenderSnapshot>& snap);

    void UpdateDrawFrame();
}
