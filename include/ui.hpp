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

namespace ui {
    // Immutable, render-ready view of the system
    // Producer: simulation thread, Consumer: render thread
    // Positions & orbit points: relative to the center body => render thread can re-project them under the current zoom every frame
    struct RenderSnapshot {
        struct Body {
            str name;
            Vec3 position;
            float radius_2d = 0;
            float radius_3d = 0;
            std::optional<Color> color;
            const PlanetVisual* planet_visual = nullptr;
            bool rendering_visible = false;
        };
        struct DetailedBody {
            str name;
            Vec3 position;
            Vec3 velocity;
            double mass;
            std::optional<Color> color;
        };
        struct Orbit {
            std::vector<Vec3> points;
            std::optional<Color> color;
        };
        std::array<Body,  NUM_CELESTIAL_BODIES> bodies;
        std::array<Orbit, NUM_CELESTIAL_BODIES> orbits;
        std::size_t max_orbit_points_used = 0;
        std::array<std::optional<DetailedBody>, NUM_CELESTIAL_BODIES> detailed_body_display;
    };

    struct UpdateBodyCommand {
        std::size_t i;

        std::optional<double> mass;
        std::optional<double> px, py, pz;
        std::optional<double> vx, vy, vz;
        bool disable_rendering = false;
        bool enable_rendering = false;
    };

    struct EditableDouble {
        double val = 0.0;
        bool editing = false;
    };

    struct BodyEditState {
        EditableDouble mass;
        EditableDouble px, py, pz;
        EditableDouble vx, vy, vz;
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

    inline double simulated_years() {
        return static_cast<double>(config::steps_simulated.load()) * runtime_config::time_step / config::SECONDS_PER_YEAR;
    }

    inline double measured_simulation_speed() {
        const double seconds = config::timer.seconds();
        return seconds > 0.0 ? simulated_years() / seconds : 0.0;
    }

    void load_star_background();

    void unload_star_background();

    inline ImU32 to_imgui_color(const Color color) {
        return IM_COL32(color.r, color.g, color.b, color.a);
    }

    void draw_imgui_text(const std::string& text, Vec2 pos, float font_size, Color color);

    void draw_imgui_centered_text(const str& text, Vec2 center, float angle, float font_size, Color color);

    inline void draw_imgui_circle(const Vec2& pos, const float radius, const Color color) {
        ImGui::GetBackgroundDrawList()->AddCircleFilled(
            ImVec2(
                static_cast<float>(pos.x),
                static_cast<float>(pos.y)
            ),
            radius,
            to_imgui_color(color)
        );
    }

    inline void draw_imgui_rectangle(const Vec2& a, const Vec2& b, const Color color) {
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

    inline void DrawLine(const Vec2& start_pos, const Vec2& end_pos, const float thick, const Color color) {
        DrawLineEx(Vector2(static_cast<float>(start_pos.x), static_cast<float>(start_pos.y)), Vector2(static_cast<float>(end_pos.x), static_cast<float>(end_pos.y)), thick, color);
    }

    void draw_imgui_outlined_text(const str& text, const Vector2& pos, float font_size, const Color& fill, const Color& outline);

    void draw_ui_2d();

    void draw_viewport_borders_2d();

    void draw_legend_2d(const std::shared_ptr<const RenderSnapshot>& snap);

    inline std::pair<Vec2, Vec2> center_button_coordinates() {
        return {{config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 10}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}};
    }

    inline std::pair<Vec2, Vec2> copy_button_coordinates() {
        return {{config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 50}};
    }

    void imgui_style_setup(bool dark_mode);

    void imgui_style_cleanup();

    void draw_planet_info(const std::shared_ptr<const RenderSnapshot>& snap, bool dark_mode);

    void draw_stats(const std::shared_ptr<const RenderSnapshot>& snap, bool dark_mode);

    void draw_planets_2d(const std::shared_ptr<const RenderSnapshot>& snap);

    bool clip_segment_2d(Vec3& start, Vec3& end);

    void draw_orbits_2d(const std::shared_ptr<const RenderSnapshot>& snap);

    void draw_orbits_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam);

    void draw_planets_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam);

    void draw_grid_3d(const Camera3D& cam);

    void draw_axes_3d();

    void draw_axes_labels_3d(const Camera3D& cam);

    std::optional<std::size_t> pick_body_at_mouse_2d(const std::shared_ptr<const RenderSnapshot>& snap);
    std::optional<std::size_t> pick_body_at_mouse_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam);

    void draw_planet_labels_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam);

    Camera3D make_camera();

    void draw_3d(const std::shared_ptr<const RenderSnapshot>& snap);

    void draw_2d(const std::shared_ptr<const RenderSnapshot>& snap);

    void UpdateDrawFrame();
}
