#include <cstddef>

#include "ui.hpp"

void ui_helpers::cancel_camera_drag() {
    ui_config::panning_2d = false;
    ui_config::panning_3d = false;
}

void ui_helpers::reset_view_offsets() {
    ui_config::view_offset_2d_m = {};
    cancel_camera_drag();
}

void ui_helpers::reset_view() {
    config::SCALING = config::ORIGINAL_SCALING;
    config::AXIS_SCALING = config::ORIGINAL_AXIS_SCALING;

    ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
    reset_view_offsets();

    if (ui_config::view_3d) {
        // reset azimuth/elevation/distance to defaults
        ui_config::cam_azimuth = config::DEFAULT_CAM_AZIMUTH;
        ui_config::cam_elevation = config::DEFAULT_CAM_ELEVATION;
        ui_config::cam_distance = config::DEFAULT_CAM_DISTANCE;
    }
}

void ui_helpers::set_coordinate_origin(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const std::size_t index) {
    config::center_celestial_body_index.store(index);

    if (index != config::NO_BODY_SELECTED) {
        fit_camera_to_planetary_system_3d(snap, index);
    }

    ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;

    reset_view_offsets();
    config::republish_needed = true;
}
