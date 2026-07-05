#pragma once

#include <raylib.h>

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
        std::array<Orbit, NUM_CELESTIAL_BODIES> orbits; // already decimated to max_rendered_orbit_segments_per_body
        std::size_t max_orbit_points_used = 0;
        std::optional<DetailedBody> detailed_body_display;
    };

    inline double simulated_years() {
        return static_cast<double>(config::steps_simulated.load()) * config::TIME_STEP / config::SECONDS_PER_YEAR;
    }

    inline double measured_simulation_speed() {
        const double seconds = config::timer.seconds();
        return seconds > 0.0 ? simulated_years() / seconds : 0.0;
    }

    void DrawTextCenteredEx(const Font &font, const char *text, Vec2 center, const float angle, const float fontSize, const float spacing, const Color color);

    inline void DrawCircle(const Vec2& pos, const float radius, const Color color) {
        DrawCircleV(Vector2(static_cast<float>(pos.x), static_cast<float>(pos.y)), radius, color);
    }

    void DrawRectangle(const Vec2& a, const Vec2& b, const Color color);

    inline void DrawText(const Font &font, const char *text, const Vec2 &position, const float fontSize, const float spacing, const Color color) {
        DrawTextEx(font, text, Vector2(static_cast<float>(position.x), static_cast<float>(position.y)), fontSize, spacing, color);
    }

    inline void DrawLine(const Vec2& start_pos, const Vec2& end_pos, const float thick, const Color color) {
        DrawLineEx(Vector2(static_cast<float>(start_pos.x), static_cast<float>(start_pos.y)), Vector2(static_cast<float>(end_pos.x), static_cast<float>(end_pos.y)), thick, color);
    }

    void DrawTextOutlined(const Font& font, const char* text, const Vector2& pos, const float fontSize, const float spacing, const Color& fill, const Color& outline);

    void draw_ui();

    void draw_legend(const std::shared_ptr<const RenderSnapshot>& snap);

    inline std::pair<Vec2, Vec2> center_button_coordinates() {
        return {{config::WINDOW_MARGIN + 4 * config::GRID_SPACING, config::WINDOW_MARGIN + 10}, {config::WINDOW_MARGIN + 5.5 * config::GRID_SPACING, config::WINDOW_MARGIN + 30}};
    }

    void draw_planet_info(const Color text_color, const Color background_color, const std::shared_ptr<const RenderSnapshot>& snap);

    void draw_stats(const Color text_color, const Color background_color, const std::shared_ptr<const RenderSnapshot>& snap);

    void draw_planets(const std::shared_ptr<const RenderSnapshot>& snap);

    void draw_orbits(const std::shared_ptr<const RenderSnapshot>& snap);

    void draw_orbits_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam);

    void draw_planets_3d(const std::shared_ptr<const RenderSnapshot>& snap);

    void draw_grid_3d(const Camera3D& cam);

    void draw_axes_3d();

    void draw_axes_labels_3d(const Camera3D& cam);

    int pick_body_at_mouse_2d(const std::shared_ptr<const RenderSnapshot>& snap);

    int pick_body_at_mouse_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam);

    void draw_planet_labels_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam);

    Camera3D make_camera();

    void draw_3d(const std::shared_ptr<const RenderSnapshot>& snap);

    void draw_2d(const std::shared_ptr<const RenderSnapshot>& snap);

    void UpdateDrawFrame();
};
