#include <algorithm>
#include <cmath>
#include <format>

#include "screen_utils.hpp"
#include "ui.hpp"

// 2d only

namespace {
    using namespace ui_data;
    using namespace ui_helpers;

    double body_radius_pixels_2d(const RenderSnapshot::Body& body) {
        const double marker_radius = body.radius_2d * ui_config::marker_scale;

        if (ui_config::body_size_mode == ui_config::BodySizeMode::schematic) return marker_radius;

        // TODO: What if the screen isn't square?
        constexpr double viewport_width = config::WINDOW_WIDTH - 2.0 * config::WINDOW_MARGIN;
        const double pixels_per_meter = viewport_width / (2.0 * config::SCALING);

        switch (ui_config::body_size_mode) {
            case ui_config::BodySizeMode::physical:
                return body.physical_radius_m * pixels_per_meter;
            case ui_config::BodySizeMode::physical_with_markers:
                return std::max(
                    body.physical_radius_m * pixels_per_meter,
                    marker_radius
                );
            default: return 0; // Never gets reached anyways
        }
    }

    void draw_ui_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
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

    void draw_viewport_borders_2d() {
        constexpr auto margin = static_cast<float>(config::WINDOW_MARGIN);

        ImGui::GetBackgroundDrawList()->AddRect(
            {margin, margin},
            {
                static_cast<float>(config::WINDOW_WIDTH) - margin,
                static_cast<float>(config::WINDOW_HEIGHT) - margin
            },
            to_imgui_color(BLACK),
            0.0f,
            ImDrawFlags_None,
            2.0f
        );
    }

    void draw_planets_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
        const std::optional<std::size_t> hovered_body_i = pick_body_at_mouse_2d(snap);

        // Draw moons first, then planets
        for (const bool draw_moons : {true, false}) {

            // snap->bodies positions are already relative to the center body (see publish_snapshot)
            for (std::size_t i = 0; i < snap->bodies.size(); i++) {
                if (is_moon(i) != draw_moons) continue;

                const auto& body = snap->bodies[i];
                if (!body || !body->rendering_visible || !body->color) continue;

                const double radius_px = body_radius_pixels_2d(*body);
                const Vector2 pos = to_raylib(body->relative_position, ui_config::get_2d_offset(snap));

                if (!circle_intersects_viewport(pos, radius_px)) continue;

                DrawCircleV(pos, static_cast<float>(radius_px), *body->color);

                const Vector2 text_pos = {pos.x - 10, pos.y - 10};
                if (hovered_body_i && i == *hovered_body_i) {
                    draw_imgui_outlined_text(body->name, text_pos, 20, *body->color, {0, 0, 0, 125});
                }
            }
        }
    }

    bool clip_segment_2d(Vec3& start, Vec3& end, const std::shared_ptr<const RenderSnapshot>& snap) {
        // 2 simple inside_screen() checks aren't sufficient, example case:
        // start is outside on the left, end is outside on the top, and the line connecting them is partially inside the viewport
        // This is really visible when you zoom in very close into an orbit

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

    void draw_orbits_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
        ui_config::trail_segments_reduced_2d = 0;
        ui_config::trail_segments_submitted_2d = 0;

        if (!ui_config::draw_orbits) return;

        struct Cache {
            std::shared_ptr<const RenderSnapshot> snapshot;
            double scaling = 0.0;
            std::array<std::vector<std::size_t>, NUM_CELESTIAL_BODIES> indices;
        };

        static Cache cache;

        // Regenerate cache when snap/scaling changes
        if (cache.snapshot != snap || cache.scaling != config::SCALING) {
            cache.snapshot = snap;
            cache.scaling = config::SCALING;

            for (auto& indices : cache.indices) {
                indices.clear();
            }
        }

        constexpr double minimum_distance_px = 1.0;
        constexpr double minimum_distance_squared = minimum_distance_px * minimum_distance_px;

        const double pixels_per_meter = (config::WINDOW_WIDTH - 2.0 * config::WINDOW_MARGIN) / (2.0 * config::SCALING);

        const Vec2 offset = ui_config::get_2d_offset(snap);

        const std::size_t orbit_limit = ui_config::draw_moon_orbits
            ? NUM_CELESTIAL_BODIES
            : NUM_PLANETS + NUM_DWARF_PLANETS;

        for (std::size_t i = 0; i < orbit_limit; ++i) {
            const auto& [points, color] = snap->orbits[i];

            if (!color || points.size() < 2 || !snap->bodies[i] || !snap->bodies[i]->rendering_visible) continue;

            auto& indices = cache.indices[i];

            // Build only for trails we actually need
            if (indices.empty()) {
                indices.push_back(0);

                for (std::size_t j = 1; j + 1 < points.size(); ++j) {
                    const Vec3& previous = points[indices.back()];
                    const Vec3& candidate = points[j];

                    const double dx = (candidate.x - previous.x) * pixels_per_meter;
                    const double dy = (candidate.y - previous.y) * pixels_per_meter;

                    if (dx * dx + dy * dy >= minimum_distance_squared) {
                        indices.push_back(j);
                    }
                }

                indices.push_back(points.size() - 1);
            }

            ui_config::trail_segments_reduced_2d += indices.size() - 1;

            const auto original_segment_count = static_cast<float>(points.size() - 1);

            for (std::size_t k = 1; k < indices.size(); ++k) {
                const std::size_t previous_index = indices[k - 1];
                const std::size_t current_index = indices[k];

                Vec3 start = points[previous_index];
                Vec3 end = points[current_index];

                // Keep clipping: reduced segments may cross the viewport
                // even when both endpoints are outside.
                if (!clip_segment_2d(start, end, snap)) continue;

                const float age = static_cast<float>(current_index) / original_segment_count;
                const float alpha = 0.10f + 0.70f * age;

                DrawLineEx(
                    to_raylib(start, offset),
                    to_raylib(end, offset),
                    1.5f,
                    Fade(*color, alpha)
                );

                ui_config::trail_segments_submitted_2d++;
            }
        }
    }
}

// Shared 2d

std::optional<std::size_t> ui_helpers::pick_body_at_mouse_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (!snap) return std::nullopt;

    const auto [mx, my] = GetMousePosition();

    std::optional<std::size_t> best;
    float best_dist_sq = 0;

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const auto& body = snap->bodies[i];
        if (!body || !body->rendering_visible) continue;

        const auto [sx, sy] = to_raylib(body->relative_position, ui_config::get_2d_offset(snap));
        const float dx = mx - sx;
        const float dy = my - sy;
        const float dist_sq = dx * dx + dy * dy;

        const double hit_radius = std::max(body_radius_pixels_2d(*body), static_cast<double>(ui_config::MIN_CLICK_RADIUS_PX_2D));

        // If cursor is considered too far away from that celestial body, don't consider it
        if (dist_sq > hit_radius * hit_radius) continue;

        bool prefer_candidate = !best.has_value();

        if (best) {
            const bool candidate_is_moon = is_moon(i);
            const bool best_is_moon = is_moon(*best);

            // If candidate and best aren't the same type (moon vs. not moon)
            if (candidate_is_moon != best_is_moon) {
                // Prefer the candidate if it isn't a moon
                prefer_candidate = !candidate_is_moon;
            } else {
                // If candidate and best are the same type (both moons / both not moons)
                // Prefer the candidate if it is closer to the cursor
                prefer_candidate = dist_sq < best_dist_sq;
            }
        }

        if (prefer_candidate) {
            best = i;
            best_dist_sq = dist_sq;
        }
    }

    return best;
}

void ui_helpers::draw_2d(const std::shared_ptr<const RenderSnapshot>& snap) {
    if (snap) {
        draw_orbits_2d(snap);
        draw_planets_2d(snap);

        draw_imgui_rectangle({0, 0}, {config::WINDOW_MARGIN, config::WINDOW_HEIGHT}, WHITE);
        draw_imgui_rectangle({0, 0}, {config::WINDOW_WIDTH, config::WINDOW_MARGIN}, WHITE);
        draw_imgui_rectangle({0, config::WINDOW_HEIGHT - config::WINDOW_MARGIN}, {config::WINDOW_WIDTH, config::WINDOW_HEIGHT}, WHITE);
        draw_imgui_rectangle({config::WINDOW_WIDTH - config::WINDOW_MARGIN, 0}, {config::WINDOW_WIDTH, config::WINDOW_HEIGHT}, WHITE);

        draw_toolbar(snap, false);
        draw_ui_2d(snap);
        draw_viewport_borders_2d();
        draw_legend(snap, false);
        draw_planet_info(snap, false);
    }
}
