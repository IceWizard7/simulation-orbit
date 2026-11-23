#include <algorithm>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <format>
#include <imgui.h>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <raylib.h>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <misc/cpp/imgui_stdlib.h>

#include "hierarchy.hpp"
#include "orbital_elements.hpp"
#include "planet_visual.hpp"
#include "rlImGui.h"
#include "time_units.hpp"
#include "ui.hpp"

namespace {
    void request_checkpoint_restore(const std::size_t index) {
        ui_data::simulation_command_queue.push(
            ui_data::RestoreCheckpoint{index}
        );

        ui_helpers::reset_editor_drafts();
    }

    // Returns true if the selection changed
    bool reference_combo(const ui_data::RenderSnapshot& snap, std::optional<std::size_t>& reference_index, const std::optional<std::size_t> exclude, const char* label, const char* id) {
        const auto before = reference_index;

        // Defensive fallback if the reference becomes unavailable
        if (reference_index && (*reference_index >= snap.bodies.size() || *reference_index == exclude || !snap.bodies[*reference_index])) {
            reference_index.reset();
        }

        const char* reference_name = reference_index && snap.bodies[*reference_index] ? snap.bodies[*reference_index]->name.c_str() : "None (absolute)";
        const std::optional<Color> reference_color = reference_index && snap.bodies[*reference_index] ? snap.bodies[*reference_index]->color : std::nullopt;

        ImGui::SetNextItemWidth(180.0f);

        if (ui_helpers::begin_labeled_combo(label, id, reference_name, 210.0f, reference_color)) {
            if (ImGui::Selectable("None (absolute)", !reference_index)) {
                reference_index.reset();
            }

            ImGui::Separator();

            for (std::size_t ref = 0; ref < snap.bodies.size(); ref++) {
                if (ref == exclude) continue; // Body with info itself

                const auto& candidate = snap.bodies[ref];
                if (!candidate) continue;

                ImGui::PushID(static_cast<int>(ref));

                const bool already_selected = reference_index && *reference_index == ref;

                if (ui_helpers::body_selectable(candidate->name, already_selected, candidate->color)) {
                    reference_index = ref;
                }

                if (already_selected && ImGui::IsWindowAppearing()) {
                    ImGui::SetScrollHereY(0.5f);
                }

                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        return reference_index != before;
    }


    bool creation_allowed(const ui_data::RenderSnapshot& snap) {
        const auto custom_slots = std::span(snap.bodies).subspan(NUM_CATALOG_BODIES);

        return !runtime_config::use_simd
               && runtime_config::interaction_set == runtime_config::InteractionSet::full
               && std::ranges::any_of(custom_slots, [](const auto& body) { return !body.has_value(); });
    }

    // Returns the new value when it changed (set or removed)
    std::optional<std::optional<std::size_t>> texture_row(const std::optional<std::size_t> current, const std::size_t body_id) {
        // body_id: NO_BODY_SELECTED for creation panel

        std::optional<std::optional<std::size_t>> changed;

        const PlanetVisual* visual = current && *current < ui_config::body_textures.size()
                                         ? &ui_config::body_textures[*current]
                                         : nullptr;

        if (visual && visual->loaded) {
            rlImGuiImageSize(&visual->texture, 64, 32); // Smaller preview (2:1 ratio)
            ImGui::SameLine();
            ImGui::TextUnformatted(GetFileName(visual->texture_path->c_str()));
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                changed = std::optional<std::size_t>{};
            }
        } else if (visual && !visual->failed) {
            ImGui::TextDisabled("Loading %s...", GetFileName(visual->texture_path->c_str()));
        } else if (visual && visual->failed) {
            ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Could not load %s", GetFileName(visual->texture_path->c_str()));
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) changed = std::optional<std::size_t>{};
        } else {
            ImGui::TextDisabled("No texture (drop image)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::SetTooltip(
                    "Drop an image onto this window to texture the body in 3D.\n"
                    "Formats: PNG, JPG, BMP, QOI, GIF (first frame).\n"
                    "Use an equirectangular map (2:1, longitude x latitude),\n"
                    "like a planet texture; other aspect ratios look stretched.\n"
                    "Images wider than 4096 px are scaled down to save VRAM."
                );
            }
        }

        // Window-level drop target (state files never get here: receive_dropped_file() consumes them)
        if (ui_config::dropped_file && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            const auto& drop = *ui_config::dropped_file;

            if (drop.kind == ui_config::DropKind::unsupported) {
                ui_config::texture_error = std::format("Unsupported format: {}", GetFileExtension(drop.path.c_str()));
                ui_config::texture_error_source = body_id;
            } else if (const auto id = ui_helpers::register_custom_texture_file(drop.path)) {
                changed = id;
                ui_config::texture_error.clear();
            } else {
                ui_config::texture_error = "Could not load " + drop.path;
                ui_config::texture_error_source = body_id;
            }

            ui_config::dropped_file.reset(); // Consumed
        }

        if (!ui_config::texture_error.empty() && ui_config::texture_error_source == body_id) {
            ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", ui_config::texture_error.c_str());
        }

        return changed;
    }

    void export_file(ui_config::FileStatus& status, const str& filename, const str_view contents, const char* mime_type) {
        const auto saved = ui_helpers::save_user_file(filename, contents, mime_type);
        status = {saved ? std::format("Saved: {}", *saved) : "Export failed", !saved};
    }

    void status_line(const ui_config::FileStatus& status) {
        if (status.text.empty()) return;
        if (status.failed) ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.3f, 1.0f), "%s", status.text.c_str());
        else ImGui::TextDisabled("%s", status.text.c_str());
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
            row("Frame interval", std::format("{:.3f} ms", frame_ms));
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

            auto& edit = ui_data::edit_states[i];

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
                const bool reference_changed = reference_combo(*snap, edit.reference_index, i, "Edit relative to:", "##reference");

                Vec3 displayed_position = body->absolute_position;
                Vec3 displayed_velocity = body->velocity;

                if (edit.reference_index) {
                    if (const auto& reference = snap->bodies[*edit.reference_index]) {
                        displayed_position -= reference->absolute_position;
                        displayed_velocity -= reference->velocity;
                    }
                }

                if (reference_changed) {
                    edit.px = {displayed_position.x, false};
                    edit.py = {displayed_position.y, false};
                    edit.pz = {displayed_position.z, false};

                    edit.vx = {displayed_velocity.x, false};
                    edit.vy = {displayed_velocity.y, false};
                    edit.vz = {displayed_velocity.z, false};
                }

                ImGui::BeginDisabled(ui_data::get_step_progress().busy);

                if (ImGui::BeginTable("PositionVelocity", 2)) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Position");
                    ImGui::Indent(15.0f);

                    if (imgui_double_input(edit.px, displayed_position.x, i, "x", "x##position", 4, "m")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = edit.reference_index, .px = edit.px.val});
                    if (imgui_double_input(edit.py, displayed_position.y, i, "y", "y##position", 4, "m")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = edit.reference_index, .py = edit.py.val});
                    if (imgui_double_input(edit.pz, displayed_position.z, i, "z", "z##position", 4, "m")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = edit.reference_index, .pz = edit.pz.val});

                    ImGui::Spacing();
                    ImGui::Text("Distance: %.4e m", displayed_position.length());

                    ImGui::Unindent(15.0f);

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Velocity");
                    ImGui::Indent(15.0f);

                    if (imgui_double_input(edit.vx, displayed_velocity.x, i, "x", "x##velocity", 4, "m/s")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = edit.reference_index, .vx = edit.vx.val});
                    if (imgui_double_input(edit.vy, displayed_velocity.y, i, "y", "y##velocity", 4, "m/s")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = edit.reference_index, .vy = edit.vy.val});
                    if (imgui_double_input(edit.vz, displayed_velocity.z, i, "z", "z##velocity", 4, "m/s")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = edit.reference_index, .vz = edit.vz.val});

                    ImGui::Spacing();
                    ImGui::Text("Speed: %.4e m/s", displayed_velocity.length());

                    ImGui::Unindent(15.0f);

                    ImGui::EndTable();

                    if (edit.reference_index && snap->bodies[*edit.reference_index]) {
                        const auto point_masses = to_point_masses(*snap);
                        const PointMass& self = *point_masses[i];
                        const PointMass& ref = *point_masses[*edit.reference_index];

                        // Membership is needed even when merging is off, to decide whether to show the checkbox at all
                        const std::vector<std::size_t> members = members_of(snap->hierarchy, i);
                        const bool mergeable = !members.empty() && can_merge(snap->hierarchy, i, edit.reference_index);

                        if (mergeable) {
                            ImGui::Checkbox("Include moons (system barycenter)", &ui_config::orbit_use_system_barycenter);

                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
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
                                // Every member divides by the same hill, so sorting by distance = sorting by r_H fraction (and still works when hill is NaN/inf)
                                // NaN distances (non-finite edits) go last: a plain `<` on NaN breaks std::sort's strict weak ordering (undefined behavior)
                                std::vector<std::pair<double, std::size_t>> by_distance; // {distance to i, member}

                                by_distance.reserve(members.size());
                                for (const std::size_t m : members) {
                                    by_distance.emplace_back((point_masses[m]->position - self.position).length(), m);
                                }

                                std::ranges::stable_sort(by_distance, {}, [](const auto& entry) {
                                    return std::pair{std::isnan(entry.first), entry.first};
                                });

                                for (const auto& [distance, m] : by_distance) {
                                    if (std::isfinite(hill)) {
                                        ImGui::Text("%s: %.3f r_H", snap->bodies[m]->name.c_str(), distance / hill);
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
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) { ImGui::SetTooltip("Semi major axis a"); }
                                ImGui::Text("c: %.6e m", orbit->linear_eccentricity);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) { ImGui::SetTooltip("Linear eccentricity c"); }
                                ImGui::Text("i: %.4f deg", orbit->inclination * 180.0 / std::numbers::pi);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) { ImGui::SetTooltip("Inclination i"); }

                                ImGui::Unindent(15.0f);

                                ImGui::TableNextColumn();
                                ImGui::Indent(15.0f);

                                ImGui::Text("b: %.6e m", orbit->semi_minor_axis);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) { ImGui::SetTooltip("Semi minor axis b"); }
                                ImGui::Text("e: %.6f", orbit->eccentricity);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) { ImGui::SetTooltip("Eccentricity e"); }
                                ImGui::Text("τ: %s", time_units::format_duration(orbit->orbital_period).c_str());
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) { ImGui::SetTooltip("Orbital period τ"); }

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

                if (imgui_double_input(edit.mass, body->mass, i, "Mass", "mass", 6, "kg")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .mass = edit.mass.val});

                if (imgui_double_input(edit.physical_radius_m, body->physical_radius_m, i, "Radius", "radius", 6, "m")) ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .physical_radius_m = edit.physical_radius_m.val});

                ImGui::EndDisabled();

                ImGui::SameLine();

                const ImGuiStyle& style = ImGui::GetStyle();

                const bool recently_copied = std::chrono::steady_clock::now() - ui_config::last_copied[i] < std::chrono::seconds(1);

                const auto copy_values_label = recently_copied ? "Copied!" : "Copy values";
                const float copy_values_width = ImGui::CalcTextSize(copy_values_label).x + 2.0f * style.FramePadding.x;
                float group_width = copy_values_width;

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                if (ImGui::Button(std::format("{}###copy_values", copy_values_label).c_str())) {
                    // Recompute reference_name (reference_index might have changed)
                    const char* relative_name = edit.reference_index && snap->bodies[*edit.reference_index] ? snap->bodies[*edit.reference_index]->name.c_str() : "None (absolute)";

                    const str text = std::format(
                        "{}\n"
                        "Data Relative to: {}\n"
                        "Years simulated: {:.3f}\n"
                        "Position: {} m\n"
                        "Velocity: {} m/s\n"
                        "Mass: {:e} kg\n"
                        "Radius: {:e} m",
                        body->name, relative_name, simulated_years(), displayed_position.to_string(), displayed_velocity.to_string(), body->mass, body->physical_radius_m
                    );
                    SetClipboardText(text.c_str());
                    ui_config::last_copied[i] = std::chrono::steady_clock::now();
                }

                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextUnformatted("Camera");

                ImGui::SameLine();

                const auto follow_label = ui_config::followed_celestial_body_index == i ? "Stop following" : "Follow";
                constexpr auto goto_label = "Go to";
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

                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
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

                constexpr auto coordinate_origin_label = "Set origin";
                const float coordinate_origin_width = ImGui::CalcTextSize(coordinate_origin_label).x + 2.0f * style.FramePadding.x;
                group_width = coordinate_origin_width;

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                if (ImGui::Button("Set origin")) {
                    set_coordinate_origin(i);
                }

                ImGui::TextUnformatted("Data");

                ImGui::SameLine();

                constexpr auto new_graph_label = "New graph";
                group_width = ImGui::CalcTextSize(new_graph_label).x + 2.0f * style.FramePadding.x;

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                if (ImGui::Button(new_graph_label)) {
                    open_graph_window(snap, i, edit.reference_index);
                }

                ImGui::TextUnformatted("Body");
                ImGui::SameLine();

                constexpr auto delete_label = "Delete";
                constexpr auto rename_label = "Rename";

                const float rename_width = ImGui::CalcTextSize(rename_label).x + 2.0f * style.FramePadding.x;
                const float delete_width = ImGui::CalcTextSize(delete_label).x + 2.0f * style.FramePadding.x;
                const float color_picker_width = ImGui::GetFrameHeight();

                group_width = rename_width + delete_width + color_picker_width + 2.0f * style.ItemSpacing.x;

                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, ImGui::GetContentRegionAvail().x - group_width)
                );

                if (ImGui::Button(rename_label)) {
                    edit.rename_buffer = body->name; // Prefill once, on open
                    ImGui::OpenPopup("Rename body");
                }

                if (ImGui::BeginPopup("Rename body")) {
                    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

                    ImGui::SetNextItemWidth(260.0f);
                    const bool enter = ImGui::InputText("##name", &edit.rename_buffer, ImGuiInputTextFlags_EnterReturnsTrue);
                    const bool valid = !is_blank(edit.rename_buffer);

                    ImGui::BeginDisabled(!valid);
                    const bool save = ImGui::Button("Save");
                    ImGui::EndDisabled();
                    ImGui::SameLine();

                    if (valid && (enter || save)) {
                        ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .name = edit.rename_buffer});
                        ImGui::CloseCurrentPopup();
                    } else if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                ImGui::SameLine();

                edit.color_active = ImGui::IsItemActive(); // true during drags inside the picker popup too

                // Draft follows the snapshot except while the picker is being dragged
                // (otherwise it would flicker back to the old color until the publish arrives)
                if (!edit.color_active) to_float3(body->color.value_or(WHITE), edit.color);

                if (ImGui::ColorEdit3("##body_color", edit.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoTooltip)) {
                    ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .color = to_color(edit.color)});
                }

                edit.color_active = ImGui::IsItemActive(); // Must come right after ColorEdit3; Also true during picker drags

                if (body->texture_id && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    ImGui::SetTooltip("Textured in 3D; color applies to 2D, trails, legend and labels");
                }

                ImGui::SameLine();

                // Every body can be deleted, including the last one (empty scenario)
                const bool can_delete = !runtime_config::use_simd && runtime_config::interaction_set == runtime_config::InteractionSet::full;

                ImGui::BeginDisabled(!can_delete);

                if (ImGui::Button("Delete")) {
                    ImGui::OpenPopup("Delete body?");
                }

                if (confirm_delete_popup("Delete body?", std::format("Delete {}?", body->name),
                                         "Restoring a checkpoint brings it back if it existed then.", dark_mode)) {
                    ui_data::simulation_command_queue.push(ui_data::DeleteBodyCommand{i});
                }

                ImGui::EndDisabled();


                if (const auto t = texture_row(body->texture_id, i)) {
                    // Changed this frame?
                    if (*t) {
                        ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .texture_id = *t});
                    } else {
                        ui_data::simulation_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .clear_texture = true});
                    }
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
    const str center_name = center_i < snap->bodies.size() && snap->bodies[center_i] ? snap->bodies[center_i]->name : "Absolute (inertial frame)";
    const double target = runtime_config::target_simulation_speed.load();
    const str target_speed = target > 0.0 ? std::format("{:.3f} yr/s", target) : "Unlimited";

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
            stat("TIME", std::format("{:.3f} yr", years));
            const double t = static_cast<double>(snap->step) * runtime_config::time_step;

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::SetTooltip(
                    "Local time:  %s\n"
                    "Simulation:  %s\n\n"
                    "Simulation time (TDB) is uniform. Local time converts it to UTC\n"
                    "(leap seconds from 1972, an estimate of Earth's rotation before: +-1 s)\n"
                    "and then to your time zone. Dates before ~1890 use local mean time.\n"
                    "TIME counts 365-day years; dates use the real (gregorian) calendar.",
                    time_units::format_local_time(t).c_str(),
                    time_units::format_epoch_date(t).c_str()
                );
            }

            ImGui::TableSetColumnIndex(1);
            stat("AVERAGE SPEED", std::format("{:.3f} yr/s", simulation_speed));

            ImGui::TableSetColumnIndex(2);
            stat("STEP", runtime_config::time_step_string);

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            stat("RUN TIME", std::format("{:.3f} s", seconds));

            ImGui::TableSetColumnIndex(1);

            if (progress.busy) {
                stat(
                    "SIMULATING",
                    std::format(
                        "{} / {} steps ({:.3f}%)",
                        group_thousands(progress.completed),
                        group_thousands(progress.total),
                        100.0 * static_cast<double>(progress.completed) / static_cast<double>(progress.total)
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

                if (ImGui::Selectable("Absolute (inertial frame)", none_selected) && !none_selected) {
                    set_coordinate_origin(config::NO_BODY_SELECTED);
                }

                ImGui::Separator();

                for (std::size_t i = 0; i < snap->bodies.size(); ++i) {
                    const auto& body = snap->bodies[i];
                    if (!body) continue;

                    ImGui::PushID(static_cast<int>(i));

                    const bool already_selected = center_i == i;

                    if (body_selectable(body->name, already_selected, body->color) && !already_selected) {
                        set_coordinate_origin(i);
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

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
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

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
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
                    ui_config::pending_camera_fit = center;
                }
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::SetTooltip("Switch between 2D and 3D (T)");
            }

            // Trail toggles
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slider_gap);

            if (ImGui::Button("Tools", ImVec2(-1.0f, 0.0f))) {
                ImGui::OpenPopup("##ToolOptions");
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
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

                if (ImGui::SliderFloat("Speed##simulation", &speed, 0.001f, 100.0f, "%.3f yr/s", ImGuiSliderFlags_Logarithmic)) {
                    runtime_config::target_simulation_speed = speed;
                }

                double target_simulation_speed = runtime_config::target_simulation_speed.load();

                // Whole steps are enough; slow speeds (below 10 steps/s) keep one decimal
                const double steps_per_second = target_simulation_speed * config::SECONDS_PER_YEAR / runtime_config::time_step;
                const str steps_text = target_simulation_speed == -1.0 ? "Unlimited"
                                       : steps_per_second < 10.0 ? std::format("{:.1f}", steps_per_second)
                                       : group_thousands(static_cast<std::uint64_t>(std::llround(steps_per_second)));

                ImGui::TextDisabled("Steps/s: %s", steps_text.c_str());

                ImGui::EndDisabled();

                ImGui::Separator();

                ImGui::MenuItem("Checkpoints...", nullptr, &ui_config::show_checkpoints);
                ImGui::MenuItem("Diagnostics...", nullptr, &ui_config::show_diagnostics);


                const bool has_bodies = std::ranges::any_of(
                    snap->bodies,
                    [](const auto& body) {
                        return static_cast<bool>(body);
                    }
                );

                if (ImGui::MenuItem("New graph...", nullptr, false, has_bodies)) open_graph_window(snap); // Allow multiple graph windows

                ImGui::Separator();

                // Keep the popup open, so the result line below stays visible
                ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

                if (ImGui::MenuItem("Export history (CSV)")) {
                    export_file(ui_config::csv_export_status, std::format("history_step{}.csv", snap->step), full_state_csv(snap), "text/csv");
                }

                ImGui::SetItemTooltip(
                    "Positions (m), velocities (m/s) and masses (kg) of all bodies, sampled like the graphs.\n"
                    "Absolute coordinates in the stored inertial frame; units m, m/s, kg.\n"
                    "\n"
                    "Each edit (mass, position, velocity, create, delete) starts a new segment.\n"
                    "Rows with the same \"segment\" number belong together; position edits make the data jump.\n"
                    "Analyze each segment separately, e.g. don't compute velocities across a boundary.\n"
                    "\n"
                    "For research data, use --headless --csv."
                );

                status_line(ui_config::csv_export_status);

                if (ImGui::MenuItem("Export state (JSON)", nullptr, false, snap != nullptr)) {
                    export_file(ui_config::json_export_status, std::format("state_step{}.json", snap->step), state_json(*snap), "application/json");
                }

                ImGui::SetItemTooltip(
                    "Current positions, velocities, GM, radii, J2 data and colors of all enabled bodies.\n"
                    "Absolute, stored inertial frame. Can be imported later (format v1)."
                );

#ifdef __EMSCRIPTEN__
                // Opened by the browser in the touch/mouse-up handler (needs a user gesture), not by this click
                ImGui::MenuItem("Import state (JSON)...");
                {
                    const bool armed = ImGui::IsItemHovered() || ImGui::IsItemActive();
                    arm_state_file_picker(armed);
                }
#else
                ImGui::BeginDisabled();
                ImGui::MenuItem("Import state (JSON)...");
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Drop a .json file onto the window");
                }
#endif

                status_line(ui_config::json_export_status);
                status_line(ui_config::import_status);

                ImGui::PopItemFlag();

                const double dt = runtime_config::time_step;

                if (ImGui::MenuItem(std::format("Step once ({})", time_units::format_duration(dt, 3)).c_str(), nullptr, false, can_step)) {
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

                // This parameter can not be made a constant expression. It would result in a compiler error.
                // ReSharper disable once CppVariableCanBeMadeConstexpr
                const str_view text(step_count_text);
                step_t step_count = 0;

                const auto [end, error] = std::from_chars(
                    text.data(), text.data() + text.size(), step_count
                );

                const bool valid = error == std::errc{} && end == text.data() + text.size() && step_count > 0;

                if (valid) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("= %s", time_units::format_duration(static_cast<double>(step_count) * dt, 3).c_str());
                }

                ImGui::BeginDisabled(!can_step || !valid);

                if ((ImGui::Button("Run steps") || enter) && valid && can_step) {
                    ui_data::request_steps(step_count);
                }

                ImGui::EndDisabled();

                if (ImGui::MenuItem("Create body...", nullptr, false, creation_allowed(*snap))) {
                    ui_config::show_create_body = true;
                }

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

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
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

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
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

        ImGui::BeginDisabled(!has_checkpoints);

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

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    ImGui::SetTooltip("%.6f years", static_cast<double>(checkpoint.simulated_seconds) / config::SECONDS_PER_YEAR);
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

            const bool valid = str_view(rename_buffer).find_first_not_of(" \t\r\n") != str_view::npos;

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

        ImGui::BeginDisabled(!has_checkpoints);

        if (ImGui::Button("Restore selected")) {
            request_checkpoint_restore(selected_id);
        }

        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(!has_checkpoints || selected_id == 0);

        if (ImGui::Button("Delete selected")) {
            ImGui::OpenPopup("Delete checkpoint?");
        }

        const auto selected = std::ranges::find(snap->checkpoints, selected_id, &ui_data::CheckpointInfo::id);
        const str question = selected != snap->checkpoints.end() ? std::format("Delete \"{}\"?", selected->label) : "Delete checkpoint?";

        if (confirm_delete_popup("Delete checkpoint?", question, "This cannot be undone.", dark_mode)) {
            ui_data::simulation_command_queue.push(ui_data::DeleteCheckpoint{selected_id});

            // Keep the selection in place: The entry just above (ID 0 is first and protected, so it exists)
            selected_id = selected != snap->checkpoints.begin() && selected != snap->checkpoints.end()
                              ? std::prev(selected)->id
                              : 0;
        }

        ImGui::EndDisabled();

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

                if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
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

void ui_helpers::draw_create_body_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const bool dark_mode) {
    if (!ui_config::show_create_body || !snap) return;

    auto& draft = ui_config::create_body_draft;

    imgui_style_setup(dark_mode);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);

    if (ImGui::Begin("Create body", &ui_config::show_create_body, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        // Escape closes the window, but not while an input is active (Escape there reverts the field first)
        // Checked before the widgets, so the item still counts as active from last frame
        if (!ui_config::escape_consumed && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ui_config::show_create_body = false;
        }

        ImGui::BeginDisabled(ui_data::get_step_progress().busy);

        const float properties_label_width = ImGui::CalcTextSize("Radius:").x + ImGui::GetStyle().ItemSpacing.x;
        const float axis_label_width = ImGui::CalcTextSize("x:").x + ImGui::GetStyle().ItemSpacing.x;
        constexpr float field_width = 200.0f;

        // Properties
        ImGui::InputText("Name", &draft.name);
        imgui_draft_double_input(draft.mass, "Mass", "mass", 6, "kg", properties_label_width, field_width);
        imgui_draft_double_input(draft.radius_m, "Radius", "radius", 6, "m", properties_label_width, field_width);
        ImGui::ColorEdit3("Color", draft.color, ImGuiColorEditFlags_NoInputs);

        if (const auto t = texture_row(draft.texture_id, config::NO_BODY_SELECTED)) {
            draft.texture_id = *t;
        }

        // Reference
        ImGui::Separator();
        reference_combo(*snap, draft.reference_index, std::nullopt, "Relative to:", "##create_reference");
        const ui_data::RenderSnapshot::Body* reference = draft.reference_index && snap->bodies[*draft.reference_index] ? &*snap->bodies[*draft.reference_index] : nullptr;

        // Position / velocity table
        if (ImGui::BeginTable("CreatePositionVelocity", 2)) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Position");
            imgui_draft_double_input(draft.position.x, "x", "x##p", 4, "m", axis_label_width, field_width);
            imgui_draft_double_input(draft.position.y, "y", "y##p", 4, "m", axis_label_width, field_width);
            imgui_draft_double_input(draft.position.z, "z", "z##p", 4, "m", axis_label_width, field_width);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Velocity");
            imgui_draft_double_input(draft.velocity.x, "x", "x##v", 4, "m/s", axis_label_width, field_width);
            imgui_draft_double_input(draft.velocity.y, "y", "y##v", 4, "m/s", axis_label_width, field_width);
            imgui_draft_double_input(draft.velocity.z, "z", "z##v", 4, "m/s", axis_label_width, field_width);

            ImGui::EndTable();
        }
        ImGui::TextDisabled("Relative to %s", reference ? reference->name.c_str() : "Absolute (inertial frame)");


        // Circular
        CircularOrbit c = circular_orbit_velocity(
            draft.position,
            reference ? config::GRAVITATIONAL_CONSTANT * (reference->mass + draft.mass) : 0.0
        );

        ImGui::BeginDisabled(c.issue != CircularOrbitIssue::none);
        if (ImGui::Button("Circular orbit")) {
            draft.velocity = c.velocity;
        }
        ImGui::EndDisabled();

        if (c.issue != CircularOrbitIssue::none && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            const char* circular_problem =
                c.issue == CircularOrbitIssue::coincident ? "Position coincides with the reference" : c.issue == CircularOrbitIssue::pole_axis ? "Position on the pole axis: direction undefined" : c.issue == CircularOrbitIssue::non_positive_mu ? "Total mass must be positive and finite" : "Unreachable";

            ImGui::SetTooltip("%s", circular_problem);
        }

        // Timescale for the velocity arrow (and drag)

        if (c.issue == CircularOrbitIssue::none && reference) {
            // Reference is implied anyway
            const double lambda = draft.velocity.length() / c.velocity.length();
            ImGui::Text("Speed: %.3f x circular", lambda);
            ImGui::SameLine();
            ImGui::TextDisabled("(escape at %.3f)", std::numbers::sqrt2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::SetTooltip(
                    "Arrow as long as the distance to %s = circular speed.\n"
                    "< 1: body is at apoapsis, > 1: at periapsis, >= sqrt(2): escapes.",
                    reference->name.c_str()
                );
            }
        }

        const auto mu = create_draft_mu(*snap);

        if (mu && reference) {
            // Reference is implied anyway
            const Vec3 h = draft.position.cross(draft.velocity);
            const auto el = orbital_elements(draft.position, draft.velocity, *mu);

            if (el && h.length() > 0.0) {
                constexpr ImVec4 WARN{1.0f, 0.7f, 0.2f, 1.0f};
                const double e = el->eccentricity;
                const double p = h.dot(h) / *mu; // h.dot(h) = h.length() * h.length() (slight performance optimization)
                const double peri = p / (1.0 + e);

                ImGui::Text("Eccentricity: %.4f", e);
                ImGui::Text("Periapsis: %.4e m", peri);

                if (std::isfinite(el->orbital_period)) {
                    const double apo = p / (1.0 - e);
                    ImGui::Text("Apoapsis: %.4e m", apo);
                    ImGui::Text("Period: %s", time_units::format_duration(el->orbital_period, 3).c_str());

                    const double r_H = snap->hierarchy.hill_radius[*draft.reference_index];
                    if (std::isfinite(r_H) && r_H > 0.0) {
                        ImGui::Text("Apoapsis: %.3f r_H", apo / r_H);
                        if (apo > 0.5 * r_H) {
                            ImGui::TextColored(WARN, "Likely unstable: Beyond ~0.5 r_H of %s", reference->name.c_str());
                        }
                    }
                } else {
                    const double v_inf = std::sqrt(std::max(0.0, draft.velocity.dot(draft.velocity) - 2.0 * *mu / draft.position.length()));
                    ImGui::TextColored(WARN, "Escapes %s (v_inf = %.4e m/s)", reference->name.c_str(), v_inf);
                }

                if (peri < reference->physical_radius_m) {
                    ImGui::TextColored(WARN, "Periapsis inside %s (no collisions modeled)", reference->name.c_str());
                }
            } else if (el) {
                ImGui::TextDisabled("Radial velocity: falls straight in or out");
            }
        }

        const bool placing = ui_config::body_placement_state != ui_config::BodyPlacementState::idle;
        if (ImGui::Button(placing ? "Cancel placing" : "Place in view")) {
            if (placing) {
                cancel_placement();
            } else {
                ui_config::body_placement_backup = draft;
                ui_config::body_placement_state = ui_config::BodyPlacementState::armed;
            }
        }
        if (placing) ImGui::TextDisabled("Click: position · drag: velocity · Esc: cancel");

        // Validation and creation
        const auto finite = [](const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };

        // First problem wins; nullptr = the draft can be created
        const auto validation_problem = [&]() -> const char* {
            if (!creation_allowed(*snap)) return "No free slot (or --simd / reduced interaction set)";
            if (is_blank(draft.name)) return "Name is empty";
            if (!std::isfinite(draft.mass)) return "Mass must be finite";
            if (!std::isfinite(draft.radius_m) || draft.radius_m < 0) return "Radius must be finite and >= 0";
            if (!finite(draft.position) || !finite(draft.velocity)) return "Position and velocity must be finite";
            if (reference && draft.position.length() == 0.0) return "Position coincides with the reference";
            return nullptr;
        };
        const char* problem = validation_problem();

        ImGui::Separator();
        ImGui::BeginDisabled(problem != nullptr);
        if (ImGui::Button("Create")) {
            ui_data::simulation_command_queue.push(ui_data::CreateBodyCommand{
                .reference_index   = draft.reference_index,
                .position          = draft.position,
                .velocity          = draft.velocity,
                .mass              = draft.mass,
                .physical_radius_m = draft.radius_m,
                .name              = draft.name,
                .color             = to_color(draft.color),
                .texture_id        = draft.texture_id
            });

            draft.name = "Body " + std::to_string(++ui_config::created_body_counter);
        }
        ImGui::EndDisabled(); // problem != nullptr
        ImGui::EndDisabled(); // Step progress busy (start of window)

        if (problem != nullptr) {
            ImGui::TextDisabled("%s", problem);
        }
    }

    ImGui::End();
    imgui_style_cleanup();
}

void ui_helpers::draw_scene_context_menu(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const bool dark_mode) {
    if (!snap) return;

    if (ui_config::scene_context_menu_requested) {
        ImGui::OpenPopup("##scene_context"); // Opens at the current mouse position
        ui_config::scene_context_menu_requested = false;
    }

    imgui_style_setup(dark_mode);
    if (ImGui::BeginPopup("##scene_context")) {
        const bool allowed = creation_allowed(*snap);
        auto& draft = ui_config::create_body_draft;

        if (ImGui::MenuItem("Create body here", nullptr, false, allowed)) {
            if (set_draft_xy_from_snapshot_point(*snap, ui_config::scene_context_menu_point_m)) {
                ui_config::show_create_body = true;
            }
        }

        if (const auto i = ui_config::scene_context_menu_body; i && snap->bodies[*i]) {
            if (ImGui::MenuItem(std::format("Create body orbiting {}", snap->bodies[*i]->name).c_str(), nullptr, false, allowed)) {
                draft.reference_index = *i;
                ui_config::show_create_body = true;
            }
        }

        ImGui::EndPopup();
    }
    imgui_style_cleanup();
}

void ui_helpers::draw_empty_scene_overlay() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    // Same font path as the 2D labels; background draw list: above the scene, below every ImGui window
    draw_imgui_centered_text(
        "No bodies. Tools > Create body... or right-click / double-tap the scene (2D) to add one.",
        {center.x, center.y}, 0.0f, 16.0f, GRAY
    );
}
