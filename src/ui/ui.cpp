#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <rlImGui.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


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
        ui_config::drag_last_mouse_px = GetMousePosition();
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

    if (!ImGui::GetIO().WantCaptureKeyboard && !ui_data::get_step_progress().busy && IsKeyPressed(config::KEY_RESUME_AND_PAUSE)) {
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

#ifdef __EMSCRIPTEN__
    // Raw browser delta (pixels, + = down), drained every frame
    const double raw_wheel = EM_ASM_DOUBLE({ const d = Module.wheelDeltaY; Module.wheelDeltaY = 0; return d; });
    const float wheel = static_cast<float>(-raw_wheel / config::WEB_WHEEL_PIXELS_PER_ZOOM_STEP);
#else
    const float wheel = GetMouseWheelMoveV().y;
#endif

    const float scroll = ImGui::GetIO().WantCaptureMouse ? 0.0f : wheel;

    // Two fingers: Zoom at the midpoint; the first finger must not keep panning/rotating
    const Pinch pinch = read_pinch();
    if (pinch.active) cancel_camera_drag();
    const bool pinch_ok = pinch.active && !ImGui::GetIO().WantCaptureMouse;

    const auto [mx, my] = pinch.active ? pinch.center : GetMousePosition();

    // Signed zoom amount this frame: + = in, - = out
    const bool keys_ok = !ImGui::GetIO().WantCaptureKeyboard;
    const double zoom_steps = std::clamp(static_cast<double>(scroll), -3.0, 3.0) // limit huge flicks
        + (pinch_ok ? pinch.log_scale / std::log(config::ZOOM_FACTOR) : 0.0)  // fingers track the scene 1:1
        + (keys_ok && zoom_key_down(true) ? 1.0 : 0.0)
        - (keys_ok && zoom_key_down(false) ? 1.0 : 0.0);
    const bool shift = std::ranges::any_of(config::KEYS_SHIFT, IsKeyDown);

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
            const auto [x, y] = consume_drag_delta();

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

        if (zoom_steps != 0.0) {
            const double base = shift ? config::ZOOM_3D_FACTOR*config::ZOOM_3D_FACTOR : config::ZOOM_3D_FACTOR;
            // zoom_steps > 0 -> closer (divide), < 0 -> farther (multiply)
            ui_config::cam_distance *= static_cast<float>(std::pow(base, -zoom_steps));
        }

        ui_config::cam_distance = std::clamp(ui_config::cam_distance, ui_config::MIN_CAM_DISTANCE, ui_config::MAX_CAM_DISTANCE);
        if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_RESET)) {
            reset_view();
        }
    } else {
        if (!ui_config::panning_2d && !ImGui::GetIO().WantCaptureMouse && // Currently not panning, ImGui doesn't consume mouse
            IsWindowFocused() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && // Window focused & button is pressed
            !pick_body_at_mouse_2d(snap) && // Drag doesn't start on a celestial body
            config::WINDOW_MARGIN <= mx && mx <= static_cast<float>(GetScreenWidth()) - config::WINDOW_MARGIN && // Mouse is in viewport (x & y)
            config::WINDOW_MARGIN <= my && my <= static_cast<float>(GetScreenHeight()) - config::WINDOW_MARGIN) {

            ui_config::panning_2d = true;
            ui_config::drag_last_mouse_px = GetMousePosition();
        }

        if (ui_config::panning_2d) {
            const auto [dx, dy] = consume_drag_delta();
            const double pixels_per_meter = get_viewport_2d().pixels_per_meter;

            ui_config::view_offset_2d_m.x -= dx / pixels_per_meter;
            ui_config::view_offset_2d_m.y -= dy / pixels_per_meter;
        }

        // Two-finger pan: The scene follows the finger midpoint (same sign convention as mouse panning)
        if (pinch_ok) {
            const double pixels_per_meter = get_viewport_2d().pixels_per_meter;
            ui_config::view_offset_2d_m.x -= pinch.center_delta.x / pixels_per_meter;
            ui_config::view_offset_2d_m.y -= pinch.center_delta.y / pixels_per_meter;
        }

        if (zoom_steps != 0.0) {
            const bool zoom_in = zoom_steps > 0;
            const bool zoom_out = !zoom_in;
            const bool anchor_at_mouse = config::WINDOW_MARGIN <= mx && mx <= static_cast<float>(GetScreenWidth()) - config::WINDOW_MARGIN
                && config::WINDOW_MARGIN <= my && my <= static_cast<float>(GetScreenHeight()) - config::WINDOW_MARGIN;
            const Vec2 before = screen_to_meters({mx, my}, ui_config::get_2d_offset(snap));
            const double base = shift ? config::ZOOM_FACTOR * config::ZOOM_FACTOR : config::ZOOM_FACTOR;
            const double zoom_factor = std::pow(base, std::abs(zoom_steps));

            if (zoom_in) {
                // scrolled up (=> in)
                if (config::AXIS_SCALING / zoom_factor < 4.0 / 3) {
                    config::AXIS_SCALING *= 10;
                    config::AXIS_SCALING /= zoom_factor;
                    config::SCALING /= zoom_factor;
                } else {
                    config::AXIS_SCALING /= zoom_factor;
                    config::SCALING /= zoom_factor;
                }
            } else if (zoom_out) {
                // scrolled down (=> out)
                if (config::AXIS_SCALING * zoom_factor > 10 * 4.0 / 3) {
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

    draw_checkpoint_window(snap, ui_config::view_3d);
    draw_diagnostics_window(snap, ui_config::view_3d);
    draw_graph_windows(snap, ui_config::view_3d);

    rlImGuiEnd();
    EndDrawing();
}
