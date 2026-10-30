#include <algorithm>
#include <iostream>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include "screen_utils.hpp"
#include "simulation.hpp"
#include "ui.hpp"

// 3d only

namespace {
    using namespace ui_data;
    using namespace ui_helpers;

    [[nodiscard]] float projected_radius_pixels_3d(const Vector3& world_position, const float world_radius, const Camera3D& cam) {
        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
        const Vector2 center_screen = GetWorldToScreen(world_position, cam);
        const Vector2 edge_screen = GetWorldToScreen(
            Vector3Add(world_position, Vector3Scale(right, world_radius)),
            cam
        );

        return Vector2Distance(center_screen, edge_screen);
    }

    [[nodiscard]] float adaptive_body_radius_3d(const RenderSnapshot::Body& body, const std::size_t body_index, const Camera3D& cam, const Vec3& render_origin) {
        const auto physical_radius_world = static_cast<float>(body.physical_radius_m / config::WORLD_UNIT_METERS);

        if (ui_config::body_size_mode == ui_config::BodySizeMode::physical) {
            return physical_radius_world;
        }

        const Vector3 world_position = to_world(body.relative_position, render_origin);
        const float projected_radius = projected_radius_pixels_3d(world_position, body.radius_3d, cam);

        if (!std::isfinite(projected_radius) || projected_radius <= 0.0f) {
            const float fallback_radius = body.radius_3d * ui_config::marker_scale;

            return ui_config::body_size_mode ==
                ui_config::BodySizeMode::physical_with_markers
                ? std::max(physical_radius_world, fallback_radius)
                : fallback_radius;
        }

        const float min_radius = is_moon(body_index) ? ui_config::MOON_MIN_RADIUS_PX_3D : ui_config::PLANET_MIN_RADIUS_PX_3D;
        const float max_radius = is_moon(body_index) ? ui_config::MOON_MAX_RADIUS_PX_3D : ui_config::PLANET_MAX_RADIUS_PX_3D;
        const float desired_radius = std::clamp(projected_radius, min_radius, max_radius);

        const float schematic_radius_world = body.radius_3d * desired_radius / projected_radius * ui_config::marker_scale;

        if (ui_config::body_size_mode == ui_config::BodySizeMode::physical_with_markers) {
            return std::max(physical_radius_world, schematic_radius_world);
        }

        return schematic_radius_world;
    }

    [[nodiscard]] double camera_near_plane_3d(const Camera3D& cam) {
        return std::max(1.0e-9, static_cast<double>(Vector3Distance(cam.position, cam.target)) * 1.0e-3);
    }

    [[nodiscard]] double camera_far_plane_3d(const Camera3D& cam, const std::shared_ptr<const RenderSnapshot>& snap) {
        const Vec3 render_origin = ui_config::get_3d_offset(snap);
        double far_plane = std::max(camera_near_plane_3d(cam) * 1.0e6, 1.0e-6);
        if (!snap) return far_plane;

        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            constexpr double DEPTH_MARGIN = 1.05;
            const auto& body = snap->bodies[i];
            if (!body) continue;

            const Vector3 world_position = to_world(body->relative_position, render_origin);
            const double center_depth = Vector3DotProduct(
                Vector3Subtract(world_position, cam.position),
                forward
            );
            const double outer_depth = center_depth + adaptive_body_radius_3d(*body, i, cam, render_origin);
            if (!std::isfinite(outer_depth) || outer_depth <= 0.0) continue;

            far_plane = std::max(far_plane, outer_depth * DEPTH_MARGIN);
        }

        return far_plane;
    }

    void draw_planet_labels_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
        const Vec3 render_origin = ui_config::get_3d_offset(snap);
        const std::optional<std::size_t> hovered_body_i = pick_body_at_mouse_3d(snap, cam);

        if (!hovered_body_i) return;

        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

        const auto& body = snap->bodies[*hovered_body_i];
        if (!body || !body->color || !body->rendering_visible) return;

        const Vector3 world = to_world(body->relative_position, render_origin);
        // GetWorldToScreen doesn't clip points behind the camera
        // it can project them to a wrong on-screen spot, so skip those segments
        if (Vector3DotProduct(Vector3Subtract(world, cam.position), forward) <= 0) return;

        Vector2 text_pos = GetWorldToScreen(world, cam);
        text_pos.y -= 10;
        text_pos.x += 10;
        draw_imgui_outlined_text(body->name, text_pos, 20, *body->color, {0, 0, 0, 125});
    }

    void draw_orbits_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
        if (!snap || !ui_config::draw_orbits) return;

        const Vec3 render_origin = ui_config::get_3d_offset(snap);
        constexpr double minimum_distance_px = 1.0;
        constexpr double minimum_distance_squared = minimum_distance_px * minimum_distance_px;

        // This projection assumes CAMERA_PERSPECTIVE, as make_camera() currently uses
        const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
        const Vector3 up = Vector3CrossProduct(right, forward);

        const double focal_px = 0.5 * GetScreenHeight() / std::tan(0.5 * static_cast<double>(cam.fovy) * DEG2RAD);

        // Screen coordinates relative to the screen center
        // Translation and Y inversion don't affect distance comparisons
        const auto project = [&](const Vec3& point) -> std::optional<Vec2> {
            const Vector3 relative = Vector3Subtract(to_world(point, render_origin), cam.position);

            const double depth = Vector3DotProduct(relative, forward);

            if (depth <= 0.0) return std::nullopt;

            const double x =
                focal_px * Vector3DotProduct(relative, right) / depth;
            const double y =
                focal_px * Vector3DotProduct(relative, up) / depth;

            if (!std::isfinite(x) || !std::isfinite(y)) {
                return std::nullopt;
            }

            return Vec2{x, y};
        };

        // Compare actual camera parameters, including camera "Follow" (render_origin)
        const std::array<double, 16> camera_key = {
            cam.position.x, cam.position.y, cam.position.z,
            cam.target.x, cam.target.y, cam.target.z,
            cam.up.x, cam.up.y, cam.up.z,
            cam.fovy,
            render_origin.x,
            render_origin.y,
            render_origin.z,
            static_cast<double>(cam.projection),
            static_cast<double>(GetScreenWidth()),
            static_cast<double>(GetScreenHeight())
        };

        struct Segment {
            std::size_t first;
            std::size_t last;
        };

        struct Cache {
            std::shared_ptr<const RenderSnapshot> snapshot;
            std::array<double, 16> camera_key{};
            std::array<std::vector<Segment>, NUM_CELESTIAL_BODIES> segments;
            std::array<bool, NUM_CELESTIAL_BODIES> built{};
        };

        static Cache cache;

        // Regenerate cache when snap/camera changes
        if (cache.snapshot != snap || cache.camera_key != camera_key) {
            cache.snapshot = snap;
            cache.camera_key = camera_key;
            cache.built.fill(false);

            for (auto& segments : cache.segments) {
                segments.clear();
            }
        }

        const std::size_t orbit_limit = ui_config::draw_moon_orbits ? NUM_CELESTIAL_BODIES : NUM_PLANETS + NUM_DWARF_PLANETS;

        for (std::size_t i = 0; i < orbit_limit; ++i) {
            const auto& [points, color] = snap->orbits[i];

            if (!color || points.size() < 2 ||
                !snap->bodies[i] || !snap->bodies[i]->rendering_visible) {
                continue;
            }

            auto& segments = cache.segments[i];

            if (!cache.built[i]) {
                std::optional<std::size_t> retained;
                Vec2 retained_screen{};

                const auto finish_run = [&](const std::size_t last) {
                    if (retained && *retained != last) {
                        segments.push_back({*retained, last});
                    }
                };

                for (std::size_t j = 0; j < points.size(); ++j) {
                    const auto screen = project(points[j]);

                    if (!screen) {
                        // Preserve the endpoint of the preceding visible run.
                        if (j > 0) finish_run(j - 1);

                        retained.reset();
                        continue;
                    }

                    if (!retained) {
                        retained = j;
                        retained_screen = *screen;
                        continue;
                    }

                    const double dx = screen->x - retained_screen.x;
                    const double dy = screen->y - retained_screen.y;

                    if (dx * dx + dy * dy >= minimum_distance_squared) {
                        segments.push_back({*retained, j});
                        retained = j;
                        retained_screen = *screen;
                    }
                }

                // Preserve the final endpoint if the last run reaches it.
                finish_run(points.size() - 1);
                cache.built[i] = true;
            }

            const auto original_segment_count = static_cast<float>(points.size() - 1);

            for (const auto& [first, last] : segments) {
                const float age =
                    static_cast<float>(last) / original_segment_count;
                const float alpha = 0.10f + 0.70f * age;

                DrawLine3D(
                    to_world(points[first], render_origin),
                    to_world(points[last], render_origin),
                    Fade(*color, alpha)
                );
            }
        }
    }

    void draw_planets_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
        const Vec3 render_origin = ui_config::get_3d_offset(snap);

        for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            const auto& body = snap->bodies[i];
            if (!body || !body->rendering_visible) continue;

            const float radius_3d = adaptive_body_radius_3d(*body, i, cam, render_origin);
            if (body->planet_visual != nullptr && body->planet_visual->loaded) {
                DrawModel(
                    body->planet_visual->model,
                    to_world(body->relative_position, render_origin),
                    radius_3d,
                    WHITE // Don't tint texture
                );
            }
            else if (body->color) {
                // fallback for bodies without a texture
                DrawSphereEx(to_world(body->relative_position, render_origin), radius_3d, 32, 32, *body->color);
            }
        }
    }

    void draw_grid_3d(const Camera3D& cam) {
        constexpr int half_grid = 5;
        const auto near_epsilon = static_cast<float>(camera_near_plane_3d(cam));

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

    void draw_axes_3d() {
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

    void draw_axes_labels_3d(const Camera3D& cam) {
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

    void draw_star_background_3d(const Camera3D& cam, const double far_plane) {
        if (!ui_config::star_background_loaded) return;

        const auto radius = static_cast<float>(far_plane * 0.5);

        // We are looking at the sphere from inside
        rlDisableBackfaceCulling();


        // The sky must not hide planets drawn afterwards
        rlDisableDepthMask();

        DrawModelEx(
            ui_config::star_model,
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

// Shared 3d


void ui_helpers::load_star_background() {
    ui_config::star_model = LoadModelFromMesh(make_equirectangular_sphere_mesh(1.0f, 64, 128));
    ui_config::star_texture = LoadTexture("resources/8k_stars_milky_way.jpg");
    // Alternative: "resources/stars.jpg"

    if (ui_config::star_texture.id == 0) {
        std::cerr << "Could not load star background.\n";
        UnloadModel(ui_config::star_model);
        ui_config::star_model = {};
        return;
    }

    GenTextureMipmaps(&ui_config::star_texture);
    SetTextureFilter(ui_config::star_texture, TEXTURE_FILTER_TRILINEAR); // smooth sampling between pixels
    SetMaterialTexture(
        &ui_config::star_model.materials[0],
        MATERIAL_MAP_ALBEDO,
        ui_config::star_texture
    );

    ui_config::star_background_loaded = true;
}

void ui_helpers::unload_star_background() {
    if (!ui_config::star_background_loaded) return;

    UnloadTexture(ui_config::star_texture);
    UnloadModel(ui_config::star_model);

    ui_config::star_texture = {};
    ui_config::star_model = {};
    ui_config::star_background_loaded = false;
}

std::optional<std::size_t> ui_helpers::pick_body_at_mouse_3d(const std::shared_ptr<const RenderSnapshot>& snap, const Camera3D& cam) {
    if (!snap) return std::nullopt;

    const Vec3 render_origin = ui_config::get_3d_offset(snap);
    const auto [mx, my]   = GetMousePosition();
    const Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

    std::optional<std::size_t> best;
    float best_cam_dist = 0; // front-most body under the cursor wins

    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const auto& body = snap->bodies[i];
        if (!body ||!body->rendering_visible) continue;

        const Vector3 world = to_world(body->relative_position, render_origin);

        // Behind the camera → GetWorldToScreen would give a bogus spot, so skip
        if (Vector3DotProduct(Vector3Subtract(world, cam.position), forward) <= 0) continue;

        const auto [sx, sy] = GetWorldToScreen(world, cam);
        const float dx = mx - sx;
        const float dy = my - sy;

        const float rendered_radius = adaptive_body_radius_3d(*body, i, cam, render_origin);
        const Vector3 edge = Vector3Add(world, Vector3Scale(right, rendered_radius));
        const auto [ex, ey] = GetWorldToScreen(edge, cam);
        const float radius_px  = std::hypot(ex - sx, ey - sy); // sphere radius in pixels
        const float hit_radius = std::max(radius_px, ui_config::MIN_CLICK_RADIUS_PX_3D);

        if (dx * dx + dy * dy > hit_radius * hit_radius) continue;

        const float cam_dist = Vector3Distance(cam.position, world);

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
                // Prefer the candidate if it is closer to the camera
                prefer_candidate = cam_dist < best_cam_dist;
            }
        }

        if (prefer_candidate) {
            best = i;
            best_cam_dist = cam_dist;
        }
    }

    return best;
}

void ui_helpers::fit_camera_to_planetary_system_3d(const std::shared_ptr<const RenderSnapshot>& snap, const std::size_t center_index) {
    // Move camera to centered celestial body

    if (!snap) return;

    const auto system = std::ranges::find_if(
        simulation::planetary_systems,
        [center_index](const PlanetarySystem& candidate) {
            return candidate.parent_index == center_index;
        }
    );
    if (system == simulation::planetary_systems.end()) return;

    if (!snap->bodies[center_index]) return;

    const Vec3 parent_position = snap->bodies[center_index]->relative_position;
    double system_radius_meters = 0.0;
    for (const std::size_t moon_index : system->moon_indices) {
        const auto& moon = snap->bodies[moon_index];
        if (!moon) continue;

        system_radius_meters = std::max(
            system_radius_meters,
            (moon->relative_position - parent_position).length()
        );
    }
    if (system_radius_meters <= 0.0) return;

    // Keep the outermost modeled moon within 70% of the vertical half-view.
    constexpr double VIEW_FILL = 0.70;
    const double system_radius_world = system_radius_meters / config::WORLD_UNIT_METERS;
    constexpr double half_vertical_fov = ui_config::CAMERA_FOVY * DEG2RAD / 2.0;
    const double fitted_distance = system_radius_world / (VIEW_FILL * std::tan(half_vertical_fov));
    ui_config::cam_distance = std::clamp(
        static_cast<float>(fitted_distance),
        ui_config::MIN_CAM_DISTANCE,
        ui_config::MAX_CAM_DISTANCE
    );
}

Camera3D ui_helpers::make_camera_3d() {
    Camera3D c{};
    c.target = {0.0f, 0.0f, 0.0f};
    c.up = {0, 1, 0};
    c.fovy = ui_config::CAMERA_FOVY;
    c.projection = CAMERA_PERSPECTIVE; // alternative: CAMERA_ORTHOGRAPHIC
    c.position = {
        ui_config::cam_distance * cosf(ui_config::cam_elevation) * cosf(ui_config::cam_azimuth),
        ui_config::cam_distance * sinf(ui_config::cam_elevation),
        ui_config::cam_distance * cosf(ui_config::cam_elevation) * sinf(ui_config::cam_azimuth)
    };

    return c;
}

void ui_helpers::draw_3d(const std::shared_ptr<const RenderSnapshot>& snap) {
    const Camera3D cam = make_camera_3d();
    const double far_plane = camera_far_plane_3d(cam, snap);
    rlSetClipPlanes(camera_near_plane_3d(cam), far_plane);

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
        draw_toolbar(snap, true);
        draw_legend(snap, true);
        draw_planet_info(snap, true);
    }
}
