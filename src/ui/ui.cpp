#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <imgui.h>
#include <memory>
#include <mutex>
#include <optional>
#include <raylib.h>
#include <rlImGui.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


#include "screen_utils.hpp"

namespace {
#ifdef __EMSCRIPTEN__
    // On-screen keyboard bridge (web/shell.html): Pull typed text/keys from the hidden <input> into ImGui
    void forward_on_screen_keyboard() {
        ImGuiIO& io = ImGui::GetIO();

        int c;
        while ((c = EM_ASM_INT({return Module.pendingChars.length ? Module.pendingChars.shift() : 0; })) != 0) {
            io.AddInputCharacter(static_cast<unsigned int>(c));
        }

        int k;
        while ((k = EM_ASM_INT({return Module.pendingKeys.length ? Module.pendingKeys.shift() : 0; })) != 0) {
            const ImGuiKey key = k == 1 ? ImGuiKey_Backspace : k == 2 ? ImGuiKey_Enter : ImGuiKey_Escape;
            io.AddKeyEvent(key, true); // ImGui's event queue trickles press/release over frames
            io.AddKeyEvent(key, false);
        }

        // Publish for the touchend handler; hide the keyboard when the field closes
        static bool was_wanted = false;
        const bool wanted = io.WantTextInput;
        EM_ASM({Module.wantTextInput = !!$0; }, wanted);
        if (was_wanted && !wanted) EM_ASM({document.getElementById("ime").blur(); });
        was_wanted = wanted;
    }
#endif

    // Last pointer was a finger (web/shell.html tracks pointerType). Touch gestures can synthesize right-button
    // events, so touch uses a double tap instead of right-click; a mouse on the same device keeps right-click
    bool touch_pointer() {
#ifdef __EMSCRIPTEN__
        // @formatter:off
        return EM_ASM_INT({ return Module.lastPointerType === "touch" ? 1 : 0; }) != 0;
        // @formatter:on
#else
        return false; // Desktop GLFW reports no touch
#endif
    }

    struct DoubleTap {
        double last_tap_time = -1.0; // Release time of the last short, still tap; -1 = none
        Vector2 last_tap_px{};
        Vector2 press_px{};
    };

    DoubleTap double_tap;

    constexpr double DOUBLE_TAP_SECONDS = 0.30;
    constexpr float DOUBLE_TAP_SLOP_PX = 24.0f; // A finger never lands on the same pixel twice

    float distance_px(const Vector2 a, const Vector2 b) {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    // True on the press of a second short tap near the first one; pans and pinches never count
    bool detect_double_tap() {
        const Vector2 pointer = GetMousePosition();

        if (GetTouchPointCount() >= 2) {
            double_tap.last_tap_time = -1.0; // A pinch is never part of a double tap
            return false;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            double_tap.press_px = pointer;
            const bool second = double_tap.last_tap_time >= 0.0
                                && GetTime() - double_tap.last_tap_time <= DOUBLE_TAP_SECONDS
                                && distance_px(pointer, double_tap.last_tap_px) <= DOUBLE_TAP_SLOP_PX;
            if (second) double_tap.last_tap_time = -1.0; // A third tap starts over
            return second;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            // Only a still tap counts; the release of a pan does not
            const bool tap = distance_px(pointer, double_tap.press_px) <= DOUBLE_TAP_SLOP_PX;
            double_tap.last_tap_time = tap ? GetTime() : -1.0;
            double_tap.last_tap_px = pointer;
        }

        return false;
    }
}

void ui_helpers::UpdateDrawFrame() {
    std::shared_ptr<const ui_data::RenderSnapshot> snap;
    {
        std::lock_guard lock(config::snapshot_lock);
        snap = config::latest_snapshot;
    }

#ifdef __EMSCRIPTEN__
    // Web keyboard inputs on touch-only displays
    forward_on_screen_keyboard();
#endif

    // Automatic camera fitting
    if (ui_config::pending_camera_fit && snap && snap->center_index == *ui_config::pending_camera_fit) {
        if (ui_config::view_3d) {
            fit_camera_to_planetary_system_3d(snap, *ui_config::pending_camera_fit);
        }
        ui_config::pending_camera_fit.reset();
    }

    // Reset escape consumed
    ui_config::escape_consumed = false;

#ifdef __EMSCRIPTEN__
    arm_state_file_picker(false);
    receive_picked_state_file();
#endif

    // Handle dropped files
    receive_dropped_file();

    // Finished background decodes -> GPU (At most 1 per frame; needs the GL context, before BeginDrawing)
    poll_texture_uploads();

    // Sync indices of bodies
    sync_body_identities(snap);

    // Some keybinds (0-9, KEY_FOLLOW_NONE)
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        std::optional<std::size_t> new_center_i;

        if (IsKeyPressed(KEY_ZERO)) { new_center_i = 0; }
        if (IsKeyPressed(KEY_ONE)) { new_center_i = 1; }
        if (IsKeyPressed(KEY_TWO)) { new_center_i = 2; }
        if (IsKeyPressed(KEY_THREE)) { new_center_i = 3; }
        if (IsKeyPressed(KEY_FOUR)) { new_center_i = 4; }
        if (IsKeyPressed(KEY_FIVE)) { new_center_i = 5; }
        if (IsKeyPressed(KEY_SIX)) { new_center_i = 6; }
        if (IsKeyPressed(KEY_SEVEN)) { new_center_i = 7; }
        if (IsKeyPressed(KEY_EIGHT)) { new_center_i = 8; }
        if (IsKeyPressed(KEY_NINE)) { new_center_i = 9; }
        if (IsKeyPressed(config::KEY_FOLLOW_NONE)) { new_center_i = config::NO_BODY_SELECTED; }

        // Don't allow bodies that are invisible in rendering
        if (new_center_i && (*new_center_i == config::NO_BODY_SELECTED ||
                             (snap && *new_center_i < snap->bodies.size() && snap->bodies[*new_center_i] && snap->bodies[*new_center_i]->rendering_visible))) {
            set_coordinate_origin(*new_center_i);
        }
    }

    handle_placement_input(snap);

    const bool placing_body = ui_config::body_placement_state != ui_config::BodyPlacementState::idle;
    auto [mx, my] = GetMousePosition(); // Later modified to be used with pinch
    const bool in_viewport_2d =
        config::WINDOW_MARGIN <= mx && mx <= static_cast<float>(GetScreenWidth()) - config::WINDOW_MARGIN &&
        config::WINDOW_MARGIN <= my && my <= static_cast<float>(GetScreenHeight()) - config::WINDOW_MARGIN;

    // Scene create menu (2D): right-click with a mouse, double tap with a finger
    const bool touch = touch_pointer();
    const bool double_tapped = touch && !ui_config::view_3d && detect_double_tap();
    const bool open_scene_menu = touch ? double_tapped : IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

    if (!ui_config::view_3d && !placing_body && !ImGui::GetIO().WantCaptureMouse && in_viewport_2d && open_scene_menu) {
        ui_config::scene_context_menu_requested = true;
        ui_config::scene_context_menu_point_m = screen_to_meters(GetMousePosition(), ui_config::get_2d_offset(snap));
        ui_config::scene_context_menu_body = pick_body_at_mouse_2d(snap);
    }

    // Cancel an existing gesture (3d drag)
    if (!ui_config::view_3d || !IsWindowFocused() || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        ui_config::panning_3d = false;
    }

    // Start only on a fresh press outside ImGui (3d drag)
    if (ui_config::view_3d && !placing_body && IsWindowFocused() &&
        !ImGui::GetIO().WantCaptureMouse &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        !pick_body_at_mouse_3d(snap, make_camera_3d())) {
        ui_config::panning_3d = true;
        ui_config::drag_last_mouse_px = GetMousePosition();
    }

    // The second tap of a double tap opens the menu; it must not toggle the inspector the first tap opened
    if (!double_tapped && !placing_body && !ImGui::GetIO().WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        std::optional<std::size_t> selected_body_i;

        if (ui_config::view_3d) {
            selected_body_i = pick_body_at_mouse_3d(snap, make_camera_3d());
        } else {
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
            ui_config::pending_camera_fit = config::center_celestial_body_index.load();
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
    const double raw_wheel = EM_ASM_DOUBLE({const d = Module.wheelDeltaY; Module.wheelDeltaY = 0; return d; });
    const float wheel = static_cast<float>(-raw_wheel / config::WEB_WHEEL_PIXELS_PER_ZOOM_STEP);
#else
    const float wheel = GetMouseWheelMoveV().y;
#endif

    const float scroll = ImGui::GetIO().WantCaptureMouse ? 0.0f : wheel;

    // Two fingers: Zoom at the midpoint; the first finger must not keep panning/rotating
    const Pinch pinch = read_pinch();
    if (pinch.active) cancel_camera_drag();
    const bool pinch_ok = pinch.active && !ImGui::GetIO().WantCaptureMouse;

    if (pinch.active) {
        mx = pinch.center.x;
        my = pinch.center.y;
    } else {
        mx = GetMousePosition().x;
        my = GetMousePosition().y;
    }

    // Signed zoom amount this frame: + = in, - = out
    const bool keys_ok = !ImGui::GetIO().WantCaptureKeyboard;
    const double zoom_steps = std::clamp(static_cast<double>(scroll), -3.0, 3.0) // limit huge flicks
                              + (pinch_ok ? pinch.log_scale / std::log(config::ZOOM_FACTOR) : 0.0) // fingers track the scene 1:1
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

        if (zoom_steps != 0.0) {
            const double base = shift ? config::ZOOM_3D_FACTOR * config::ZOOM_3D_FACTOR : config::ZOOM_3D_FACTOR;
            // zoom_steps > 0 -> closer (divide), < 0 -> farther (multiply)
            ui_config::cam_distance *= static_cast<float>(std::pow(base, -zoom_steps));
        }

        ui_config::cam_distance = std::clamp(ui_config::cam_distance, ui_config::MIN_CAM_DISTANCE, ui_config::MAX_CAM_DISTANCE);
        if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(config::KEY_RESET)) {
            reset_view();
        }
    } else {
        if (!double_tapped && !placing_body && !pinch.active && !ui_config::panning_2d && !ImGui::GetIO().WantCaptureMouse && // Currently not panning, ImGui doesn't consume mouse
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
            const bool anchor_at_mouse = config::WINDOW_MARGIN <= mx && mx <= static_cast<float>(GetScreenWidth()) - config::WINDOW_MARGIN
                                         && config::WINDOW_MARGIN <= my && my <= static_cast<float>(GetScreenHeight()) - config::WINDOW_MARGIN;
            const Vec2 before = screen_to_meters({mx, my}, ui_config::get_2d_offset(snap));
            const double base = shift ? config::ZOOM_FACTOR * config::ZOOM_FACTOR : config::ZOOM_FACTOR;
            const double zoom_factor = std::pow(base, std::abs(zoom_steps));

            // Zoom changes the visible half-extent; clamped in both directions
            const double target = zoom_in ? config::SCALING / zoom_factor : config::SCALING * zoom_factor;
            config::SCALING = std::clamp(target, config::MIN_SCALING_M, config::MAX_SCALING_M);

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

    if (snap && populated_body_count(*snap) == 0) draw_empty_scene_overlay();

    if (ui_config::view_debug) draw_debug_overlay(snap);

    draw_checkpoint_window(snap, ui_config::view_3d);
    draw_diagnostics_window(snap, ui_config::view_3d);
    draw_graph_windows(snap, ui_config::view_3d);
    draw_create_body_window(snap, ui_config::view_3d);
    draw_scene_context_menu(snap, ui_config::view_3d);

    rlImGuiEnd();

    // Reads this frame's back buffer, so: After the UI is drawn, before the buffer swap
    // -> Can use the drawn pixels to take screenshots correctly
    capture_pending_graph();

    EndDrawing();
}
