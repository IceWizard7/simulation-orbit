#include "ui.hpp"

#include <algorithm>
#include <rlImGui.h>

#include "screen_utils.hpp"

void ui_helpers::UpdateDrawFrame() {
    std::shared_ptr<const ui_data::RenderSnapshot> snap;
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
        if (new_center_i && (*new_center_i == config::NO_BODY_SELECTED ||
            (snap && *new_center_i < snap->bodies.size() && snap->bodies[*new_center_i] && snap->bodies[*new_center_i]->rendering_visible))) {
            set_coordinate_origin(snap, *new_center_i);
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
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        !pick_body_at_mouse_3d(snap, make_camera_3d())) {

        ui_config::panning_3d = true;
    }

    if (!ImGui::GetIO().WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        std::optional<std::size_t> selected_body_i;

        if (ui_config::view_3d) {
            selected_body_i = pick_body_at_mouse_3d(snap, make_camera_3d());
        }
        else {
            selected_body_i = pick_body_at_mouse_2d(snap);
        }

        if (selected_body_i) {
            if (config::is_planet_info_selected(*selected_body_i)) {
                config::unhighlight_planet_info(*selected_body_i);
                ui_data::was_open[*selected_body_i] = false;
            } else {
                config::highlight_planet_info(*selected_body_i);
            }

            // Request new info for DetailedBody
            config::republish_needed = true;
        }
    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_RESET)) {
        reset_view();
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
            fit_camera_to_planetary_system_3d(snap, config::center_celestial_body_index.load());
        }
    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_LEGEND_TOGGLE)) {
        ui_config::legend_collapsed = !ui_config::legend_collapsed;
        ui_config::legend_collapse_requested = true;
    }

    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_ORBIT_TOGGLE)) ui_config::draw_orbits = !ui_config::draw_orbits;
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_MOON_ORBIT_RENDERING_TOGGLE)) ui_config::draw_moon_orbits = !ui_config::draw_moon_orbits;
    if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_DEBUG_TOGGLE)) ui_config::view_debug = !ui_config::view_debug;

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

        if (ui_config::panning_3d) {
            const auto [x, y] = GetMouseDelta();

            ui_config::cam_azimuth += x * config::DRAG_SENSITIVITY; // left/right = spin
            ui_config::cam_elevation += y * config::DRAG_SENSITIVITY; // up/down = tilt
            cam_turned = true;
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

        ui_config::cam_distance = std::clamp(ui_config::cam_distance, ui_config::MIN_CAM_DISTANCE, ui_config::MAX_CAM_DISTANCE);
        if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_RESET)) {
            reset_view();
        }
    } else {
        if (!ui_config::panning_2d && !ImGui::GetIO().WantCaptureMouse && // Currently not panning, ImGui doesn't consume mouse
            IsWindowFocused() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && // Window focused & button is pressed
            !pick_body_at_mouse_2d(snap) && // Drag doesn't start on a celestial body
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

    if (ui_config::view_debug) draw_debug_overlay(snap);

    rlImGuiEnd();
    EndDrawing();
}
