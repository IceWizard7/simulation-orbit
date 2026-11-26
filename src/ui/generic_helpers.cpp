#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <optional>
#include <raylib.h>
#include <rlgl.h>
#include <system_error>
#include "orbital_elements.hpp"

#include "screen_utils.hpp"
#include "ui.hpp"

namespace {
    void forget_body_slot(const std::size_t i) {
        config::unhighlight_planet_info(i); // Close inspector
        ui_data::edit_states[i] = {}; // Discard drafts
        ui_data::was_open[i] = false;

        if (ui_config::create_body_draft.reference_index == i) ui_config::create_body_draft.reference_index.reset();

        for (auto& state : ui_data::edit_states) {
            if (state.reference_index == i) state = {}; // Drafts were in that frame; reference becomes None too
        }

        if (ui_config::followed_celestial_body_index == i) {
            // Stop following like the existing "Stop following" action does
            ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
        }

        ui_helpers::forget_graph_body(i);
    }

    // Texture, state file, or neither; decided once per drop so every consumer sees the same answer
    ui_config::DropKind classify_drop(const str& path) {
        if (IsFileExtension(path.c_str(), ".json")) return ui_config::DropKind::state;
        if (IsFileExtension(path.c_str(), ui_config::TEXTURE_FILE_EXTENSIONS)) return ui_config::DropKind::texture;
        return ui_config::DropKind::unsupported;
    }
}

Vec3 ui_helpers::snapshot_origin_absolute(const ui_data::RenderSnapshot& snap) {
    const std::size_t center_index = config::center_celestial_body_index.load();

    return center_index < NUM_CELESTIAL_BODIES && snap.bodies[center_index] ? snap.bodies[center_index]->absolute_position : Vec3{0, 0, 0};
}

std::optional<Vec3> ui_helpers::create_ghost_preview_position(const ui_data::RenderSnapshot& snap) {
    if (!ui_config::show_create_body) return std::nullopt;

    const auto origin = snapshot_origin_absolute(snap);

    const auto& draft = ui_config::create_body_draft;

    if (draft.reference_index) {
        const auto& ref = snap.bodies[*draft.reference_index];
        return ref ? std::optional{ref->relative_position + draft.position} : std::nullopt;
    }

    return draft.position - origin;
}

bool ui_helpers::set_draft_xy_from_snapshot_point(const ui_data::RenderSnapshot& snap, const Vec2 point) {
    auto& draft = ui_config::create_body_draft;

    if (draft.reference_index) {
        const auto& ref = snap.bodies[*draft.reference_index];
        if (!ref) return false;
        draft.position.x = point.x - ref->relative_position.x; // ghost_rel = ref_rel + p  =>  p = point - ref_rel
        draft.position.y = point.y - ref->relative_position.y;
        return true;
    }

    const auto origin = snapshot_origin_absolute(snap);
    draft.position.x = point.x + origin.x; // ghost_rel = p - c  =>  p = point + c
    draft.position.y = point.y + origin.y;
    return true;
}

void ui_helpers::cancel_placement() {
    if (ui_config::body_placement_state == ui_config::BodyPlacementState::idle) return;

    ui_config::create_body_draft = ui_config::body_placement_backup;
    ui_config::body_placement_state = ui_config::BodyPlacementState::idle;
}

namespace {
    // Cursor on the placement plane (snapshot-relative meters); z = ghost z
    std::optional<Vec3> cursor_placement_point(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
        const auto ghost = ui_helpers::create_ghost_preview_position(*snap);
        if (!ghost) return std::nullopt; // Window closed, no origin, reference missing

        if (ui_config::view_3d) {
            // Ray-plane intersection with the horizontal plane through the ghost
            return ui_helpers::cursor_on_plane_3d(snap, ui_helpers::make_camera_3d(), ghost->z);
        }

        // 2D: the screen is the x/y plane; z is not visible, so keep the ghost's z
        const Vec2 p = screen_to_meters(GetMousePosition(), ui_config::get_2d_offset(snap));
        return Vec3{p.x, p.y, ghost->z};
    }
}

void ui_helpers::handle_placement_input(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
    using enum ui_config::BodyPlacementState;
    auto& state = ui_config::body_placement_state;

    if (state == idle) {
        // Press on the ghost: Drag its velocity directly (position stays)
        const bool on_ghost = ui_config::view_3d ? mouse_on_create_ghost_3d(snap, make_camera_3d()) : mouse_on_create_ghost_2d(snap);

        if (ui_config::show_create_body && IsWindowFocused() && !ImGui::GetIO().WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && on_ghost) {
            ui_config::body_placement_backup = ui_config::create_body_draft; // Escape restores the old velocity
            ui_config::placement_press_px = GetMousePosition();
            state = dragging;
        }

        return;
    }

    // Leaving 2D, closing the window or losing focus ends placement
    if (!ui_config::show_create_body || !IsWindowFocused()) {
        cancel_placement();
        return;
    }
    if (!ui_config::escape_consumed && !ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_ESCAPE)) {
        ui_config::escape_consumed = true;
        cancel_placement();
        return;
    }

    const auto [mx, my] = GetMousePosition();
    const auto point = cursor_placement_point(snap);
    const bool in_viewport = ui_config::view_3d || // In 3D the entire window is the scene; don't check viewport
                             (config::WINDOW_MARGIN <= mx && mx <= static_cast<float>(GetScreenWidth()) - config::WINDOW_MARGIN && config::WINDOW_MARGIN <= my && my <= static_cast<float>(GetScreenHeight()) - config::WINDOW_MARGIN);

    if (state == armed) {
        if (!ImGui::GetIO().WantCaptureMouse && in_viewport && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (point && set_draft_xy_from_snapshot_point(*snap, {point->x, point->y})) {
                ui_config::placement_press_px = {mx, my};
                state = dragging;
            }
        }
        return;
    }

    // Dragging
    auto& draft = ui_config::create_body_draft;
    const float dx = mx - ui_config::placement_press_px.x;
    const float dy = my - ui_config::placement_press_px.y;
    const bool is_drag = dx * dx + dy * dy > 4.0f * 4.0f; // Under 4 px: a plain click

    if (is_drag && point) {
        // Measured from the ghost's current position: It moves with the reference while the sim runs
        if (const auto anchor = create_ghost_preview_position(*snap)) {
            const double tau = placement_tau(*snap);
            draft.velocity.x = (point->x - anchor->x) / tau;
            draft.velocity.y = (point->y - anchor->y) / tau;
        }
    } else {
        // Click only: Keep the previous velocity
        draft.velocity.x = ui_config::body_placement_backup.velocity.x;
        draft.velocity.y = ui_config::body_placement_backup.velocity.y;
    }

    // Release: Done, values stay in the draft
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) state = idle;
}

std::optional<ui_data::RenderSnapshot::Body> ui_helpers::make_create_ghost(const ui_data::RenderSnapshot& snap) {
    if (!ui_config::show_create_body) return std::nullopt;

    const auto& draft = ui_config::create_body_draft;
    const auto position = create_ghost_preview_position(snap);
    const auto origin = snapshot_origin_absolute(snap);
    if (!position) return std::nullopt;

    // Non-finite drafts (ex. "1e400") would project to inf/NaN pixels
    const auto finite = [](const Vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite(*position) || !std::isfinite(draft.radius_m)) return std::nullopt;

    const auto* ref = draft.reference_index && snap.bodies[*draft.reference_index] ? &*snap.bodies[*draft.reference_index] : nullptr;

    return ui_data::RenderSnapshot::Body{
        .name              = draft.name,
        .relative_position = *position,
        .absolute_position = *position + origin,
        .velocity          = ref ? ref->velocity + draft.velocity : draft.velocity, // absolute, like real snapshot bodies
        .mass              = draft.mass,
        .physical_radius_m = std::max(0.0, draft.radius_m),
        .radius_2d         = config::CUSTOM_BODY_RADIUS_2D,
        .radius_3d         = config::CUSTOM_BODY_RADIUS_3D,
        .color             = to_color(draft.color),
        .rendering_visible = true,
    };
}

std::array<std::optional<PointMass>, NUM_CELESTIAL_BODIES> ui_helpers::to_point_masses(const ui_data::RenderSnapshot& snap) {
    std::array<std::optional<PointMass>, NUM_CELESTIAL_BODIES> result;

    for (std::size_t k = 0; k < snap.bodies.size(); k++) {
        if (snap.bodies[k]) {
            result[k] = PointMass{snap.bodies[k]->absolute_position, snap.bodies[k]->velocity, snap.bodies[k]->mass};
        }
    }

    return result;
}

void ui_helpers::cancel_camera_drag() {
    ui_config::panning_2d = false;
    ui_config::panning_3d = false;
    cancel_placement();
}

void ui_helpers::reset_view_offsets() {
    ui_config::view_offset_2d_m = {};
    cancel_camera_drag();
}

void ui_helpers::reset_view() {
    config::SCALING = config::ORIGINAL_SCALING;

    ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
    reset_view_offsets();

    if (ui_config::view_3d) {
        // reset azimuth/elevation/distance to defaults
        ui_config::cam_azimuth = config::DEFAULT_CAM_AZIMUTH;
        ui_config::cam_elevation = config::DEFAULT_CAM_ELEVATION;
        ui_config::cam_distance = config::DEFAULT_CAM_DISTANCE;
    }
}

void ui_helpers::set_coordinate_origin(const std::size_t index) {
    config::center_celestial_body_index.store(index);

    ui_config::pending_camera_fit = index != config::NO_BODY_SELECTED ? std::optional{index} : std::nullopt;

    ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;

    reset_view_offsets();
    config::republish_needed = true;
}

void ui_helpers::sync_body_identities(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
    if (!snap) return;

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        const std::uint64_t current = snap->bodies[i] ? snap->bodies[i]->id : config::NO_BODY_ID;

        if (ui_config::seen_body_ids_initialized && ui_config::seen_body_ids[i] != config::NO_BODY_ID && // a body was in this slot
            ui_config::seen_body_ids[i] != current) {
            // ...and is now gone or replaced
            forget_body_slot(i);
        }
        ui_config::seen_body_ids[i] = current;
    }
    ui_config::seen_body_ids_initialized = true;
}

std::optional<str> ui_helpers::save_user_file(const str& filename, const str_view contents, const char* mime_type) {
#ifdef __EMSCRIPTEN__
    // @formatter:off
    // HEAPU8 is a SharedArrayBuffer with pthreads; Blob rejects shared views, so slice() copies first
    EM_ASM({
const bytes = HEAPU8.slice($0, $0 + $1);
        const link = document.createElement("a");
               link.href = URL.createObjectURL(new Blob([bytes], {type: UTF8ToString($3)}));
               link.download = UTF8ToString($2);
               document.body.appendChild(link);
               link.click();
               link.remove();
               setTimeout(()=> URL.revokeObjectURL(link.href), 1000);
           }, contents.data(), contents.size(), filename.c_str(), mime_type); return filename;
    // @formatter:on
#else
    std::error_code error;
    std::filesystem::create_directories("exports", error);
    if (error) return std::nullopt;

    const std::filesystem::path path = std::filesystem::path("exports") / filename;
    std::ofstream file(path, std::ios::binary);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file) return std::nullopt;

    return path.string();
#endif
}

void ui_helpers::receive_dropped_file() {
    ui_config::dropped_file.reset(); // Last frame's drop was not consumed: dropped onto empty space

    if (!IsFileDropped()) return;

    const FilePathList files = LoadDroppedFiles();
    if (files.count > 0) {
        const str path = files.paths[0]; // One file per drop
        ui_config::dropped_file = ui_config::DroppedFile{path, classify_drop(path)};
    }
    UnloadDroppedFiles(files); // Copied above, so freeing is safe

    // Global kinds are handled here; window-specific ones wait for their window this frame
    if (ui_config::dropped_file && ui_config::dropped_file->kind == ui_config::DropKind::state) {
        import_state_file(ui_config::dropped_file->path);
        ui_config::dropped_file.reset();
    }
}

void ui_helpers::reset_editor_drafts() {
    for (auto& edit : ui_data::edit_states) {
        const auto reference = edit.reference_index;
        edit = {};
        edit.reference_index = reference;
    }

    cancel_camera_drag();
}
