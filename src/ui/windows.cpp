#include <algorithm>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <format>
#include <imgui.h>
#include <numbers>
#include <raylib.h>
#include <string_view>

#include "orbital_elements.hpp"
#include "time_units.hpp"
#include "ui.hpp"

namespace {
    void request_checkpoint_restore(const std::size_t index) {
        ui_data::simulation_command_queue.push(
            ui_data::RestoreCheckpoint{index}
        );

        for (auto& edit : ui_data::edit_states) {
            const auto reference = edit.reference_index;
            edit = {};
            edit.reference_index = reference;
        }

        ui_helpers::cancel_camera_drag();
    }
}

// 2d & 3d

void ui_helpers::draw_debug_overlay(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
    static auto last_wall = std::chrono::steady_clock::now();
    static auto last_cpu = std::clock();
    static double cpu_percent = 0.0;

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_wall).count();

    // Sample process CPU time every half-second
    if (elapsed >= 0.5) {
        const auto cpu_now = std::clock();

        if (cpu_now != static_cast<std::clock_t>(-1) &&
            last_cpu != static_cast<std::clock_t>(-1)) {
            const double cpu_seconds =
                static_cast<double>(cpu_now - last_cpu)
                / CLOCKS_PER_SEC;

            cpu_percent = 100.0 * cpu_seconds / elapsed;
        }

        last_wall = now;
        last_cpu = cpu_now;
    }

    std::size_t stored_points = 0;
    std::size_t candidate_segments = 0;

    if (snap) {
        for (std::size_t i = 0; i < snap->orbits.size(); ++i) {
            const auto& [points, color] = snap->orbits[i];
            stored_points += points.size();

            if (ui_config::draw_orbits && (ui_config::draw_moon_orbits || !is_moon(*snap, i)) &&
                snap->bodies[i] && snap->bodies[i]->rendering_visible &&
                color && points.size() >= 2) {
                candidate_segments += points.size() - 1;
            }
        }
    }

    const float fps = ImGui::GetIO().Framerate;
    const double frame_ms = fps > 0.0f ? 1000.0 / fps : 0.0;

    imgui_style_setup(true);

    ImGui::SetNextWindowPos({
        static_cast<float>(config::WINDOW_MARGIN) + 8.0f,
        static_cast<float>(config::WINDOW_MARGIN) + 8.0f
    });
    ImGui::SetNextWindowBgAlpha(0.85f);

    if (ImGui::Begin("##DebugOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs)) {
        ImGui::Text(
            "%s | %s | Trails %s",
            ui_config::view_3d ? "3D" : "2D",
            config::timer.is_running() ? "Running" : "Paused",
            ui_config::draw_orbits ? "ON" : "OFF"
        );

        constexpr ImGuiTableFlags table_flags =
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_NoSavedSettings;

        if (ImGui::BeginTable(
            "##DebugMetrics",
            2,
            table_flags,
            ImVec2(340.0f, 0.0f)
        )) {
            ImGui::TableSetupColumn(
                "Metric", ImGuiTableColumnFlags_WidthFixed, 190.0f
            );
            ImGui::TableSetupColumn(
                "Value", ImGuiTableColumnFlags_WidthStretch
            );

            const auto row = [](const char* label, const str& value) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(value.c_str());
            };

            const auto section = [](const char* label) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Separator();
                ImGui::TextDisabled("%s", label);
            };

            section("Performance");

            row("Process CPU", std::format("{:.1f}%", cpu_percent));
            row("FPS", std::format("{:.1f} / {}", fps, config::TARGET_FPS));
            row("Frame interval", std::format("{:.2f} ms", frame_ms));
            row("Steps", std::format("{}", config::steps_simulated.load()));

            section("Trails / frame");

            row("Moon trails", ui_config::draw_moon_orbits ? "ON" : "OFF");
            row("Snapshot points", std::format("{}", stored_points));
            row("Original segments", std::format("{}", candidate_segments));

            if (!ui_config::view_3d && snap) {
                const auto reduced = ui_config::trail_segments_reduced_2d;
                const auto submitted = ui_config::trail_segments_submitted_2d;

                row("After sampling", std::format("{}", reduced));
                row("Submitted lines", std::format("{}", submitted));

                if (candidate_segments > 0) {
                    const double removed_percent = 100.0 * (
                        1.0 - static_cast<double>(reduced)
                            / static_cast<double>(candidate_segments)
                    );

                    row(
                        "Sampling removed",
                        std::format("{:.1f}%", removed_percent)
                    );
                } else {
                    row("Sampling removed", "--");
                }

                if (reduced > 0) {
                    const double rejected_percent = 100.0 * (
                        1.0 - static_cast<double>(submitted)
                            / static_cast<double>(reduced)
                    );

                    row(
                        "Clipping rejected",
                        std::format("{:.1f}%", rejected_percent)
                    );
                } else {
                    row("Clipping rejected", "--");
                }
            }

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextDisabled("CPU: 100%% = one core");
        ImGui::TextDisabled("Frame interval includes waiting.");
    }

    ImGui::End();
    imgui_style_cleanup();
}

void ui_helpers::draw_legend(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const bool dark_mode) {
    constexpr float font_size = 16;
    constexpr float spacing = font_size + 2;
    constexpr float padding_x = 8.0f;
    constexpr float padding_y = 2.0f;

    imgui_style_setup(dark_mode);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding_x, padding_y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    std::size_t visible_count = 0;

    for (const auto& body : snap->bodies) {
        if (body && body->color) visible_count++;
    }

    constexpr auto margin = static_cast<float>(config::WINDOW_MARGIN);

    constexpr float width = margin * 0.25f + config::GRID_SPACING;
    const float available_height = static_cast<float>(GetScreenHeight()) - 2.0f * margin;

    const float rows_height = visible_count == 0
        ? 0.0f
        : font_size + static_cast<float>(visible_count - 1) * spacing;

    const float desired_height = ImGui::GetFrameHeight() + 2.0f * padding_y + rows_height;

    // Position & size
    ImGui::SetNextWindowPos(
        {
            static_cast<float>(GetScreenWidth()) - margin,
            margin
        },
        // Not resizable
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f)
    );

    ImGui::SetNextWindowSize({width, std::min(desired_height, available_height)}, ImGuiCond_Always);

    // Collapsing logic
    if (ui_config::legend_collapse_requested) {
        ImGui::SetNextWindowCollapsed(ui_config::legend_collapsed);
        ui_config::legend_collapse_requested = false;
    }

    const bool window_expanded = ImGui::Begin("Legend", nullptr, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);

    // Changes made through the title bar
    ui_config::legend_collapsed = ImGui::IsWindowCollapsed();

    // Actual UI drawing
    if (window_expanded) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 content_start = ImGui::GetCursorScreenPos();

        int row = 0;

        for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
            const auto& body = snap->bodies[i];
            if (!body || !body->color) continue;

            // We can not use body_selectable here because
            // clicking the text & clicking the colored circle are 2 different actions

            const float y = content_start.y + static_cast<float>(row) * spacing;
            constexpr float radius = 5.0f;

            const ImVec2 center = {
                content_start.x + radius,
                y + font_size * 0.5f
            };

            if (body->rendering_visible) {
                dl->AddCircleFilled(center, radius, to_imgui_color(*body->color));
            } else {
                dl->AddCircle(center, radius, to_imgui_color(*body->color));
            }

            // Real ImGui item over the circle's bounding box, so a popup or window on top blocks it
            ImGui::SetCursorScreenPos({center.x - radius, center.y - radius});

            const auto [mx, my] = ImGui::GetMousePos();
            const float dx = mx - center.x;
            const float dy = my - center.y;
            const bool inside_circle = dx * dx + dy * dy <= radius * radius;

            const bool circle_clicked = ImGui::InvisibleButton(
                std::format("##planet_visibility_{}", i).c_str(),
                ImVec2(2.0f * radius, 2.0f * radius)
            ) && inside_circle;

            const bool circle_hovered = ImGui::IsItemHovered() && inside_circle;

            if (circle_clicked) {
                if (body->rendering_visible) {
                    ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .disable_rendering = true});
                } else {
                    ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .enable_rendering = true});
                }
            }

            if (circle_hovered) {
                ImGui::SetTooltip("%s %s", body->rendering_visible ? "Hide" : "Show", body->name.c_str());
            }
            const ImVec2 pos = {content_start.x + 18.0f, y};

            dl->AddText(
                config::imgui_ui_font,
                font_size,
                pos,
                ImGui::GetColorU32(ImGuiCol_Text),
                body->name.c_str()
            );

            ImGui::SetCursorScreenPos(pos);

            if (ImGui::InvisibleButton(
                std::format("##planet_toggle_{}", i).c_str(),
                ImVec2(ImGui::GetContentRegionAvail().x, font_size)
            )) {
                if (config::is_planet_info_selected(i)) {
                    config::unhighlight_planet_info(i);
                    ui_data::was_open[i] = false;
                } else {
                    config::highlight_planet_info(i);
                }

                // Request new info for DetailedBody
                config::republish_needed = true;
            }

            row++;
        }
    }

    ImGui::End();

    ImGui::PopStyleVar(3);

    imgui_style_cleanup();
}

void ui_helpers::draw_planet_info(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const bool dark_mode) {
    imgui_style_setup(dark_mode);

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (config::is_planet_info_selected(i)) {
            const auto& body = snap->bodies[i];
            if (!body) continue;

            bool highlighted = true;

            auto& [mass, px, py, pz, vx, vy, vz, reference_index] = ui_data::edit_states[i];

            if (!ui_data::was_open[i]) {
                const float offset = static_cast<float>(ui_data::next_window_slot++ % 8) * 35.0f;

                ImGui::SetNextWindowPos(
                    ImVec2(100.0f + offset, 100.0f + offset),
                    ImGuiCond_Appearing
                );

                ui_data::was_open[i] = true;
            }

            const bool visible = ImGui::Begin(std::format("  {} ({}) ###planet_{}", body->name, i, i).c_str(), &highlighted, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

            // Draw even if collapsed
            if (body->color) {
                const ImVec2 pos = ImGui::GetWindowPos();
                const ImGuiStyle& style = ImGui::GetStyle();

                const float title_height = ImGui::GetFrameHeight();

                const float x = pos.x + style.FramePadding.x + ImGui::GetFontSize() + style.ItemInnerSpacing.x + 7.0f;
                const float y = pos.y + title_height / 2.0f;

                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                // Allow drawing inside the title bar
                draw_list->PushClipRect(
                    pos,
                    ImVec2(pos.x + ImGui::GetWindowWidth(), pos.y + title_height),
                    false
                );

                draw_list->AddCircleFilled(
                    ImVec2(x, y),
                    6.0f,
                    to_imgui_color(*body->color)
                );

                draw_list->PopClipRect();
            }

            if (visible) {
                const auto previous_reference = reference_index;

                // Defensive fallback if the reference becomes unavailable
                if (reference_index && (*reference_index >= snap->bodies.size() || *reference_index == i || !snap->bodies[*reference_index])) {
                    reference_index.reset();
                }

                const char* reference_name = reference_index && snap->bodies[*reference_index] ? snap->bodies[*reference_index]->name.c_str() : "None (absolute)";
                const std::optional<Color> reference_color = reference_index && snap->bodies[*reference_index] ? snap->bodies[*reference_index]->color : std::nullopt;

                ImGui::SetNextItemWidth(180.0f);

                if (begin_labeled_combo("Edit relative to:", "##reference", reference_name, 210.0f, reference_color)) {
                    if (ImGui::Selectable("None (absolute)", !reference_index)) {
                        reference_index.reset();
                    }

                    ImGui::Separator();

                    for (std::size_t ref = 0; ref < snap->bodies.size(); ref++) {
                        if (ref == i) continue; // Body with info itself

                        const auto& candidate = snap->bodies[ref];
                        if (!candidate) continue;

                        ImGui::PushID(static_cast<int>(ref));

                        const bool already_selected = reference_index && *reference_index == ref;

                        if (body_selectable(candidate->name, already_selected, candidate->color)) {
                            reference_index = ref;
                        }

                        if (already_selected && ImGui::IsWindowAppearing()) {
                            ImGui::SetScrollHereY(0.5f);
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndCombo();
                }

                Vec3 displayed_position = body->absolute_position;
                Vec3 displayed_velocity = body->velocity;

                if (reference_index) {
                    if (const auto& reference = snap->bodies[*reference_index]) {
                        displayed_position -= reference->absolute_position;
                        displayed_velocity -= reference->velocity;
                    }
                }

                if (reference_index != previous_reference) {
                    px = {displayed_position.x, false};
                    py = {displayed_position.y, false};
                    pz = {displayed_position.z, false};

                    vx = {displayed_velocity.x, false};
                    vy = {displayed_velocity.y, false};
                    vz = {displayed_velocity.z, false};
                }

                ImGui::BeginDisabled(ui_data::get_step_progress().busy);

                if (ImGui::BeginTable("PositionVelocity", 2)) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Position");
                    ImGui::Indent(15.0f);

                    if (imgui_double_input(px, displayed_position.x, i, "x", "x##position", 4, "m")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .px = px.val});
                    if (imgui_double_input(py, displayed_position.y, i, "y", "y##position", 4, "m")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .py = py.val});
                    if (imgui_double_input(pz, displayed_position.z, i, "z", "z##position", 4, "m")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .pz = pz.val});

                    ImGui::Spacing();
                    ImGui::Text("Distance: %.4e m", displayed_position.length());

                    ImGui::Unindent(15.0f);

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Velocity");
                    ImGui::Indent(15.0f);

                    if (imgui_double_input(vx, displayed_velocity.x, i, "x", "x##velocity", 4, "m/s")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .vx = vx.val});
                    if (imgui_double_input(vy, displayed_velocity.y, i, "y", "y##velocity", 4, "m/s")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .vy = vy.val});
                    if (imgui_double_input(vz, displayed_velocity.z, i, "z", "z##velocity", 4, "m/s")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .vz = vz.val});

                    ImGui::Spacing();
                    ImGui::Text("Speed: %.4e m/s", displayed_velocity.length());

                    ImGui::Unindent(15.0f);

                    ImGui::EndTable();

                    if (reference_index && snap->bodies[*reference_index]) {
                        const auto point_masses = to_point_masses(*snap);
                        const PointMass& self = *point_masses[i];
                        const PointMass& ref = *point_masses[*reference_index];

                        // Membership is needed even when merging is off, to decide whether to show the checkbox at all
                        const std::vector<std::size_t> members = members_of(snap->hierarchy, i);
                        const bool mergeable = !members.empty() && can_merge(snap->hierarchy, i, reference_index);

                        if (mergeable) {
                            ImGui::Checkbox("Include moons (system barycenter)", &ui_config::orbit_use_system_barycenter);

                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip(
                                    "On: orbit of the mass-weighted center of %s and its moons (less wobble).\n"
                                    "Off: orbit of %s alone.",
                                    body->name.c_str(), body->name.c_str()
                                );
                            }
                        }

                        if (snap->hierarchy.host[i] && !members.empty()) {
                            const double hill = snap->hierarchy.hill_radius[i];

                            // Collapsed by default: Some celestial bodies may have many moons
                            if (ImGui::TreeNode("##moons", "Moons (%zu)", members.size())) {
                                for (const std::size_t m : members) {
                                    if (std::isfinite(hill)) {
                                        const double fraction = (point_masses[m]->position - self.position).length() / hill;
                                        ImGui::Text("%s: %.3f r_H", snap->bodies[m]->name.c_str(), fraction);
                                    } else {
                                        ImGui::Text("%s", snap->bodies[m]->name.c_str());
                                    }
                                }

                                ImGui::TreePop();
                            }
                        }

                        const bool merged = mergeable && ui_config::orbit_use_system_barycenter;
                        const PointMass system = merged ? system_barycenter(point_masses, snap->hierarchy, i) : self;
                        const double mu = (system.mass + ref.mass) * config::GRAVITATIONAL_CONSTANT;
                        const auto orbit = orbital_elements(system.position - ref.position, system.velocity - ref.velocity, mu);

                        if (!orbit) {
                            ImGui::TextDisabled("No orbit (total mass <= 0)");
                        } else if (orbit->is_finite()) {
                            if (ImGui::BeginTable("OrbitalElements", 2)) {
                                ImGui::TableNextColumn();
                                ImGui::Indent(15.0f);

                                ImGui::TextUnformatted("Orbital elements");

                                ImGui::Text("a: %.6e m", orbit->semi_major_axis);
                                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Semi major axis a"); }
                                ImGui::Text("c: %.6e m", orbit->linear_eccentricity);
                                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Linear eccentricity c"); }
                                ImGui::Text("i: %.4f deg", orbit->inclination * 180.0 / std::numbers::pi);
                                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Inclination i"); }

                                ImGui::Unindent(15.0f);

                                ImGui::TableNextColumn();
                                ImGui::Indent(15.0f);

                                ImGui::Text("b: %.6e m", orbit->semi_minor_axis);
                                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Semi minor axis b"); }
                                ImGui::Text("e: %.6f", orbit->eccentricity);
                                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Eccentricity e"); }
                                ImGui::Text("τ: %s", time_units::format_duration(orbit->orbital_period).c_str());
                                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Orbital period τ"); }

                                ImGui::Unindent(15.0f);

                                ImGui::EndTable();
                            }
                        } else if (!std::isfinite(orbit->semi_major_axis)) {
                            ImGui::TextDisabled("Unbound (e = %.4f)", orbit->eccentricity);
                        } else {
                            ImGui::TextDisabled("Orbit undefined");
                        }
                    } else {
                        ImGui::TextDisabled("Choose \"Edit relative to\" to see orbit statistics");
                    }
                }

                ImGui::Spacing();

                if (imgui_double_input(mass, body->mass, i, "Mass", "mass", 6, "kg")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .mass = mass.val});

                ImGui::EndDisabled();

                ImGui::SameLine();

                const ImGuiStyle& style = ImGui::GetStyle();

                const bool recently_copied = std::chrono::steady_clock::now() - ui_config::last_copied[i] < std::chrono::seconds(1);

                const char* copy_values_label = recently_copied ? "Copied!" : "Copy values";
                const float copy_values_width = ImGui::CalcTextSize(copy_values_label).x + 2.0f * style.FramePadding.x;
                float group_width = copy_values_width;

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                if (ImGui::Button(std::format("{}###copy_values", copy_values_label).c_str())) {
                    // Recompute reference_name (reference_index might have changed)
                    const char* relative_name = reference_index && snap->bodies[*reference_index] ? snap->bodies[*reference_index]->name.c_str() : "None (absolute)";

                    const str text = std::format(
                        "{}\n"
                        "Data Relative to: {}\n"
                        "Years simulated: {:.2f}\n"
                        "Position: {} m\n"
                        "Velocity: {} m/s\n"
                        "Mass: {:e} kg",
                        body->name, relative_name, simulated_years(), displayed_position.to_string(), displayed_velocity.to_string(), body->mass
                    );
                    SetClipboardText(text.c_str());
                    ui_config::last_copied[i] = std::chrono::steady_clock::now();
                }

                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextUnformatted("Camera");

                ImGui::SameLine();

                const char* follow_label = ui_config::followed_celestial_body_index == i ? "Stop following" : "Follow";
                const auto goto_label = "Go to";
                const float goto_width = ui_config::view_3d ? 0 : ImGui::CalcTextSize(goto_label).x + 2.0f * style.FramePadding.x;
                const float follow_width = ImGui::CalcTextSize(follow_label).x + 2.0f * style.FramePadding.x;
                group_width = follow_width + (ui_config::view_3d ? 0.0f : goto_width + style.ItemSpacing.x);

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                ImGui::BeginGroup();

                if (!ui_config::view_3d) {
                    if (ImGui::Button(goto_label)) {
                        ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
                        ui_config::view_offset_2d_m = {body->relative_position.x, body->relative_position.y};
                        cancel_camera_drag();
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("One-time camera move");
                    }

                    ImGui::SameLine();
                }

                const bool is_center = config::center_celestial_body_index.load() == i;

                ImGui::BeginDisabled(is_center);

                if (ImGui::Button(std::format("{}###Follow_StopFollowing", follow_label).c_str())) {
                    if (ui_config::followed_celestial_body_index == i) {
                        ui_config::followed_celestial_body_index = config::NO_BODY_SELECTED;
                    } else {
                        ui_config::followed_celestial_body_index = i;
                    }

                    reset_view_offsets();
                }

                ImGui::EndDisabled();

                ImGui::EndGroup();

                ImGui::TextUnformatted("Coordinates");

                ImGui::SameLine();

                const auto coordinate_origin_label = "Set origin";
                const float coordinate_origin_width = ImGui::CalcTextSize(coordinate_origin_label).x + 2.0f * style.FramePadding.x;
                group_width = coordinate_origin_width;

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                if (ImGui::Button("Set origin")) {
                    set_coordinate_origin(snap, i);
                }

                ImGui::TextUnformatted("Data");

                ImGui::SameLine();

                const auto new_graph_label = "New graph";
                group_width = ImGui::CalcTextSize(new_graph_label).x + 2.0f * style.FramePadding.x;

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                if (ImGui::Button(new_graph_label)) {
                    open_graph_window(snap, i, reference_index);
                }
            }

            ImGui::End();

            if (!highlighted) {
                config::unhighlight_planet_info(i);
                ui_data::was_open[i] = false;
            }
        } else {
            ui_data::was_open[i] = false;
        }
    }

    if (std::ranges::none_of(ui_data::was_open, [](const bool open) { return open; })) {
        ui_data::next_window_slot = 0;
    }

    imgui_style_cleanup();
}

void ui_helpers::draw_toolbar(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const bool dark_mode) {
    const double seconds = config::timer.seconds();
    const double years = simulated_years();
    const double simulation_speed = seconds > 0.0 ? years / seconds : 0.0;

    const std::size_t center_i = config::center_celestial_body_index.load();
    const str center_name = center_i < snap->bodies.size() && snap->bodies[center_i] ? snap->bodies[center_i]->name : "None";
    const double target = runtime_config::target_simulation_speed.load();
    const str target_speed = target > 0.0 ? std::format("{:.2f} yr/s", target) : "Unlimited";

    const bool running = config::timer.is_running();
    const bool can_resume = runtime_config::target_total_simulation_time < 0.0 || runtime_config::target_total_simulation_time > simulated_years();

    const auto progress = ui_data::get_step_progress();
    const bool can_step = !progress.busy && !running && can_resume;

    imgui_style_setup(dark_mode);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_TextDisabled, dark_mode ? IM_COL32(170, 170, 170, 255) : IM_COL32(100, 100, 100, 255));

    constexpr float toolbar_height = config::WINDOW_MARGIN - 16.0f;
    const float toolbar_width = static_cast<float>(GetScreenWidth()) - 2.0f * config::WINDOW_MARGIN;

    // Not moveable + not resizable
    ImGui::SetNextWindowPos(ImVec2(config::WINDOW_MARGIN, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbar_width, toolbar_height), ImGuiCond_Always);

    ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("##SimulationStats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse)) {
        // Center text within the current table column
        auto centered_text = [](const char* text, const bool disabled) {
            const float available_width = ImGui::GetContentRegionAvail().x;
            const float text_width = ImGui::CalcTextSize(text).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                std::max(0.0f, (available_width - text_width) * 0.5f)
            );

            if (disabled) {
                ImGui::TextDisabled("%s", text);
            } else {
                ImGui::TextUnformatted(text);
            }
        };

        // Small label above normal-sized value
        auto stat = [&](const char* label, const str& value) {
            ImGui::PushFont(config::imgui_ui_font_small);
            centered_text(label, true);
            ImGui::PopFont();

            centered_text(value.c_str(), false);
        };

        // Leave space for buttons
        constexpr float buttons_width = 330.0f;
        const float table_gap = ImGui::GetStyle().ItemSpacing.x;

        const float table_width = std::max(1.0f, ImGui::GetContentRegionAvail().x - buttons_width - table_gap);

        if (ImGui::BeginTable("##StatsTable", 3, ImGuiTableFlags_SizingStretchSame, ImVec2(table_width, 0.0f))) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            stat("TIME", std::format("{:.2f} yr", years));

            ImGui::TableSetColumnIndex(1);
            stat("AVERAGE SPEED", std::format("{:.3f} yr/s", simulation_speed));

            ImGui::TableSetColumnIndex(2);
            stat("STEP", runtime_config::time_step_string);

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            stat("RUN TIME", std::format("{:.2f} s", seconds));

            ImGui::TableSetColumnIndex(1);

            if (progress.busy) {
                stat(
                    "SIMULATING",
                    std::format(
                        "{} / {} steps",
                        progress.completed,
                        progress.total
                    )
                );
            } else {
                stat("TARGET", target_speed);
            }

            ImGui::TableSetColumnIndex(2);
            // Center

            // Keep the label above the control, matching the other statistics
            const float column_x = ImGui::GetCursorPosX();

            ImGui::PushFont(config::imgui_ui_font_small);
            centered_text("ORIGIN", true);
            ImGui::PopFont();

            ImGui::SetCursorPosX(column_x);
            ImGui::SetNextItemWidth(-1.0f);

            // Give the popup more room than the narrow toolbar column
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(100.0f, 0.0f),
                ImVec2(FLT_MAX, FLT_MAX)
            );

            // The toolbar normally has zero vertical spacing, so add some (4.0f, matches planet info dropdown)
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

            if (ImGui::BeginCombo(
                "##coordinate_origin",
                center_name.c_str(),
                ImGuiComboFlags_HeightRegular
            )) {
                const bool none_selected = center_i == config::NO_BODY_SELECTED;

                if (ImGui::Selectable("None (inertial)", none_selected) && !none_selected) {
                    set_coordinate_origin(snap, config::NO_BODY_SELECTED);
                }

                ImGui::Separator();

                for (std::size_t i = 0; i < snap->bodies.size(); ++i) {
                    const auto& body = snap->bodies[i];
                    if (!body) continue;

                    ImGui::PushID(static_cast<int>(i));

                    const bool already_selected = center_i == i;

                    if (body_selectable(body->name, already_selected, body->color) && !already_selected) {
                        set_coordinate_origin(snap, i);
                    }

                    if (already_selected && ImGui::IsWindowAppearing()) {
                        ImGui::SetScrollHereY(0.5f);
                    }

                    ImGui::PopID();
                }

                ImGui::EndCombo();
            }

            ImGui::PopStyleVar();

            ImGui::EndTable();
        }

        ImGui::SameLine();

        if (ImGui::BeginTable("##ControlsTable", 3, ImGuiTableFlags_SizingStretchSame, ImVec2(buttons_width, 0.0f))) {
            ImGui::TableSetupColumn("Sizes", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableSetupColumn("Playback", ImGuiTableColumnFlags_WidthFixed, 26.0f);

            ImGui::TableNextRow();

            // Center each widget vertically within the toolbar window.
            auto center_vertically = [](const float height) {
                ImGui::SetCursorPosY(
                    (ImGui::GetWindowSize().y - height) * 0.5f
                );
            };

            // Body sizes
            ImGui::TableSetColumnIndex(0);

            constexpr float slider_gap = 4.0f;
            center_vertically(2.0f * ImGui::GetFrameHeight() + slider_gap);

            constexpr const char* size_modes[] = {
                "Schematic",
                "Physical",
                "Physical + markers"
            };

            int selected_mode = static_cast<int>(ui_config::body_size_mode);

            ImGui::SetNextItemWidth(-1.0f); // Fill this column
            if (ImGui::Combo(
                "##body_sizes", &selected_mode,
                size_modes, IM_ARRAYSIZE(size_modes)
            )) {
                ui_config::body_size_mode =
                    static_cast<ui_config::BodySizeMode>(selected_mode);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Body sizes");
            }

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slider_gap);

            ImGui::BeginDisabled(ui_config::body_size_mode == ui_config::BodySizeMode::physical);

            const float reset_width = ImGui::GetFrameHeight();
            const float gap = ImGui::GetStyle().ItemSpacing.x;

            ImGui::SetNextItemWidth(std::max(
                1.0f,
                ImGui::GetContentRegionAvail().x - reset_width - gap
            ));

            ImGui::SliderFloat(
                "##marker_scale",
                &ui_config::marker_scale,
                0.25f,
                3.0f,
                "Markers: %.2fx",
                ImGuiSliderFlags_AlwaysClamp
            );

            ImGui::SameLine();

            if (reset_icon_button("##reset_marker_scale")) {
                ui_config::marker_scale = 1.0f;
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset marker size to 1x");
            }

            ImGui::EndDisabled();

            // 2D / 3D
            ImGui::TableSetColumnIndex(1);
            center_vertically(2.0f * ImGui::GetFrameHeight() + slider_gap);

            if (ImGui::Button("2D | 3D", ImVec2(-1.0f, 0.0f))) {
                ui_config::view_3d = !ui_config::view_3d;
                cancel_camera_drag();

                const auto center = config::center_celestial_body_index.load();
                if (ui_config::view_3d && center != config::NO_BODY_SELECTED) {
                    fit_camera_to_planetary_system_3d(snap, center);
                }
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Switch between 2D and 3D (T)");
            }

            // Trail toggles
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slider_gap);

            if (ImGui::Button("Tools", ImVec2(-1.0f, 0.0f))) {
                ImGui::OpenPopup("##ToolOptions");
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Various Tools");
            }

            // Override the toolbar's zero vertical item spacing for this popup
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));

            if (ImGui::BeginPopup("##ToolOptions")) {
                ImGui::Checkbox("Show orbits (O)", &ui_config::draw_orbits);

                ImGui::BeginDisabled(!ui_config::draw_orbits);
                ImGui::Checkbox("Include moons (M)", &ui_config::draw_moon_orbits);
                ImGui::EndDisabled();

                ImGui::Separator();

                static float speed = 1.0f; // Remembered while Unlimited is on
                bool unlimited = target <= 0.0;
                if (!unlimited) speed = static_cast<float>(target);

                if (ImGui::Checkbox("Unlimited speed", &unlimited)) {
                    runtime_config::target_simulation_speed = unlimited ? -1.0 : static_cast<double>(speed);
                }

                ImGui::BeginDisabled(unlimited);
                ImGui::SetNextItemWidth(150.0f);

                if (ImGui::SliderFloat("Speed##simulation", &speed, 0.01f, 100.0f, "%.2f yr/s", ImGuiSliderFlags_Logarithmic)) {
                    runtime_config::target_simulation_speed = speed;
                }

                ImGui::EndDisabled();

                ImGui::Separator();

                ImGui::MenuItem("Checkpoints...", nullptr, &ui_config::show_checkpoints);
                ImGui::MenuItem("Diagnostics...", nullptr, &ui_config::show_diagnostics);
                if (ImGui::MenuItem("New graph...")) open_graph_window(snap); // Allow multiple graph windows

                if (ImGui::MenuItem("Step once", nullptr, false, can_step)) {
                    ui_data::request_steps(1);
                }

                static char step_count_text[32] = "1000";

                ImGui::SetNextItemWidth(150.0f);

                ImGui::BeginDisabled(!can_step);

                const bool enter = ImGui::InputText(
                    "Steps##manual_step_count",
                    step_count_text,
                    sizeof(step_count_text),
                    ImGuiInputTextFlags_CharsDecimal |
                    ImGuiInputTextFlags_EnterReturnsTrue
                );

                ImGui::EndDisabled();

                const std::string_view text(step_count_text);
                std::size_t step_count = 0;

                const auto [end, error] = std::from_chars(
                    text.data(), text.data() + text.size(), step_count
                );

                const bool valid = error == std::errc{} && end == text.data() + text.size() && step_count > 0;

                ImGui::BeginDisabled(!can_step || !valid);

                if ((ImGui::Button("Run steps") || enter) && valid && can_step) {
                    ui_data::request_steps(step_count);
                }

                ImGui::EndDisabled();

                ImGui::EndPopup();
            }

            ImGui::PopStyleVar(2);

            // Pause / resume & reset
            ImGui::TableSetColumnIndex(2);
            const float button_size = ImGui::GetFrameHeight();

            center_vertically(2.0f * button_size + slider_gap);

            const float buttons_y = ImGui::GetCursorPosY();
            const float button_x = ImGui::GetCursorPosX()
                + std::max(
                    0.0f,
                    (ImGui::GetContentRegionAvail().x - button_size) * 0.5f
                );

            ImGui::SetCursorPosX(button_x);

            const float button_width = button_size;
            const float button_height = button_size;

            ImGui::BeginDisabled(progress.busy || (!running && !can_resume));

            const bool clicked = ImGui::InvisibleButton(
                "##pause_resume",
                ImVec2(button_width, button_height)
            );

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(running ? "Pause" : "Resume");
            }

            ImGui::EndDisabled();

            if (clicked && !ui_data::get_step_progress().busy) {
                if (running) {
                    config::timer.pause();

                    // Last publish, so the UI is up-to-date
                    config::republish_needed = true;
                } else {
                    config::timer.resume();
                }
            }

            const ImVec2 button_start = ImGui::GetItemRectMin();
            const ImVec2 button_end = ImGui::GetItemRectMax();

            const ImVec2 center = {
                (button_start.x + button_end.x) * 0.5f,
                (button_start.y + button_end.y) * 0.5f
            };

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            const ImU32 color = dark_mode ? IM_COL32(170, 170, 170, 255) : IM_COL32(100, 100, 100, 255);

            if (running) {
                // Pause symbol
                draw_list->AddRectFilled(
                    {center.x - 6, center.y - 9},
                    {center.x - 0.5f, center.y + 9},
                    color
                );

                draw_list->AddRectFilled(
                    {center.x + 3.5f, center.y - 9},
                    {center.x + 9, center.y + 9},
                    color
                );
            } else {
                // Resume symbol
                draw_list->AddTriangleFilled(
                    {center.x - 6, center.y - 9},
                    {center.x + 9, center.y},
                    {center.x - 6, center.y + 9},
                    color
                );
            }

            ImGui::SetCursorPos(ImVec2(
                button_x,
                buttons_y + button_size + slider_gap
            ));

            if (reset_icon_button("##reset_view")) {
                reset_view();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset view (R)");
            }

            ImGui::EndTable();
        }
    }

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    imgui_style_cleanup();
}

void ui_helpers::draw_checkpoint_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const bool dark_mode) {
    if (!ui_config::show_checkpoints || !snap) return;

    imgui_style_setup(dark_mode);

    // Not moveable
    ImGui::SetNextWindowSize(ImVec2(420.0f, 325.0f), ImGuiCond_Always);

    // Not resizable
    if (ImGui::Begin("Checkpoints", &ui_config::show_checkpoints, ImGuiWindowFlags_NoResize)) {
        const bool busy = ui_data::get_step_progress().busy;

        ImGui::BeginDisabled(busy);

        static std::size_t selected_id = 0;
        static std::size_t rename_id = 0;
        static char rename_buffer[256]{};

        bool open_rename_popup = false;

        const bool has_checkpoints = !snap->checkpoints.empty();
        const bool can_restore = has_checkpoints && snap->checkpoint_restore_allowed;

        if (std::ranges::none_of(
            snap->checkpoints,
            [&](const auto& checkpoint) {
                return checkpoint.id == selected_id;
            }
        )) {
            selected_id = 0;
        }

        if (ImGui::Button("Save checkpoint")) {
            ui_data::simulation_command_queue.push(
                ui_data::CaptureCheckpoint{}
            );
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!can_restore);

        if (ImGui::Button("Reset simulation")) {
            request_checkpoint_restore(0);
        }

        ImGui::EndDisabled();

        ImGui::Separator();

        if (ImGui::BeginChild(
            "##checkpoint_list",
            ImVec2(0.0f, 200.0f),
            ImGuiChildFlags_Borders
        )) {
            for (const auto& checkpoint : snap->checkpoints) {
                ImGui::PushID(std::format("checkpoint_{}", checkpoint.id).c_str());

                // The fixed suffix keeps the widget ID stable when its label changes.
                const str row_label = std::format(
                    "{}###entry",
                    checkpoint.label
                );

                if (ImGui::Selectable(row_label.c_str(), selected_id == checkpoint.id)) {
                    selected_id = checkpoint.id;
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%.6f years", static_cast<double>(checkpoint.simulated_seconds) / config::SECONDS_PER_YEAR
                    );
                }

                if (ImGui::BeginPopupContextItem("##checkpoint_actions")) {
                    if (ImGui::MenuItem("Rename...", nullptr, false, checkpoint.id != 0)) {
                        selected_id = checkpoint.id;
                        rename_id = checkpoint.id;

                        std::snprintf(
                            rename_buffer,
                            sizeof(rename_buffer),
                            "%s",
                            checkpoint.label.c_str()
                        );

                        open_rename_popup = true;
                    }

                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::EndChild();

        if (open_rename_popup) {
            ImGui::OpenPopup("Rename checkpoint");
        }

        if (ImGui::BeginPopupModal("Rename checkpoint", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }

            ImGui::SetNextItemWidth(320.0f);

            const bool enter = ImGui::InputText(
                "Label",
                rename_buffer,
                sizeof(rename_buffer),
                ImGuiInputTextFlags_EnterReturnsTrue
            );

            const bool valid = std::string_view(rename_buffer).find_first_not_of(" \t\r\n") != std::string_view::npos;

            ImGui::BeginDisabled(!valid);
            const bool save = ImGui::Button("Save");
            ImGui::EndDisabled();

            ImGui::SameLine();

            const bool cancel = ImGui::Button("Cancel");
            const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);

            if (!busy && valid && (enter || save)) {
                ui_data::simulation_command_queue.push(ui_data::RenameCheckpoint{rename_id, str(rename_buffer)});

                ImGui::CloseCurrentPopup();
            } else if (cancel || escape) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::BeginDisabled(!can_restore);

        if (ImGui::Button("Restore selected")) {
            request_checkpoint_restore(selected_id);
        }

        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(!has_checkpoints || selected_id == 0);

        if (ImGui::Button("Delete selected")) {
            ui_data::simulation_command_queue.push(ui_data::DeleteCheckpoint{selected_id});

            selected_id = 0;
        }

        ImGui::EndDisabled();

        if (!snap->checkpoint_restore_allowed) {
            ImGui::TextDisabled(
                "Restoration is unavailable during CSV export."
            );
        }

        ImGui::TextDisabled("Restoring pauses the simulation.");

        ImGui::EndDisabled();
    }

    ImGui::End();
    imgui_style_cleanup();
}

void ui_helpers::draw_diagnostics_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const bool dark_mode) {
    if (!ui_config::show_diagnostics || !snap) return;

    imgui_style_setup(dark_mode);

    // Not resizable
    ImGui::SetNextWindowSize(ImVec2(660.0f, 0.0f), ImGuiCond_Always);

    if (ImGui::Begin("Diagnostics", &ui_config::show_diagnostics, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Stored inertial frame");

        constexpr ImGuiTableFlags flags =
           ImGuiTableFlags_SizingStretchProp |
           ImGuiTableFlags_BordersInnerH |
           ImGuiTableFlags_RowBg |
           ImGuiTableFlags_NoSavedSettings;

        // Left-align numbers within their table cells
        const auto number = [](const double value) {
            const str text = std::isfinite(value) ? std::format("{:.6e}", value) : "N/A";

            ImGui::TextUnformatted(text.c_str());
        };

        const auto vector_table = [&](const char* id, const char* reference_label, const Vec3& current, const Vec3& reference, const Vec3& drift) {
            if (!ImGui::BeginTable(id, 4, flags)) return;

            ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Current");
            ImGui::TableSetupColumn(reference_label);
            ImGui::TableSetupColumn("Drift");
            ImGui::TableHeadersRow();

            const auto row = [&](const char* label, const double value, const double reference_value, const double difference) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                number(value);
                ImGui::TableSetColumnIndex(2);
                number(reference_value);
                ImGui::TableSetColumnIndex(3);
                number(difference);
            };

            row("x", current.x, reference.x, drift.x);
            row("y", current.y, reference.y, drift.y);
            row("z", current.z, reference.z, drift.z);
            row("Magnitude", current.length(), reference.length(), drift.length());

            ImGui::EndTable();
        };

        ImGui::Spacing();
        ImGui::SeparatorText("Energy");

        const double magnitude = std::abs(snap->total_energy);
        const double resolution = std::nextafter(
            magnitude,
            std::numeric_limits<double>::infinity()
        ) - magnitude;

        if (ImGui::BeginTable("##diagnostics_energy", 3, flags)) {
            ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            const auto row = [&](const char* label, const double value, const char* tooltip = nullptr) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);

                if (tooltip && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tooltip);
                }

                ImGui::TableSetColumnIndex(1);
                number(value);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("J");
            };

            row("Total energy", snap->total_energy);
            row("Baseline", snap->baseline_energy);
            row("Drift", snap->energy_drift);
            row(
                "Resolution (double)", resolution,
                "Spacing between representable energy values.\n"
                "Does not estimate accumulated simulation error."
            );

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Linear momentum (kg m/s)");

        vector_table(
            "##diagnostics_momentum",
            "Baseline",
            snap->linear_momentum,
            snap->baseline_linear_momentum,
            snap->momentum_drift
        );

        ImGui::Spacing();
        ImGui::SeparatorText("Barycenter (m)");

        if (snap->barycenter_valid) {
            vector_table(
                "##diagnostics_barycenter",
                "Expected",
                snap->barycenter,
                snap->expected_barycenter,
                snap->barycenter_drift
            );
        } else {
            ImGui::TextDisabled("Barycenter unavailable for this state.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Independent of camera and editor reference");
        ImGui::TextDisabled("Baselines reset after physical edits and restores.");
    }

    ImGui::End();
    imgui_style_cleanup();
}

// TODO: macOS snapping latency
// TODO: Controls / Help page
