#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <format>
#include <imgui.h>
#include <raylib.h>

#include "ui.hpp"

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
        const std::size_t orbit_limit = ui_config::draw_moon_orbits
            ? NUM_CELESTIAL_BODIES
            : NUM_PLANETS + NUM_DWARF_PLANETS;

        for (std::size_t i = 0; i < snap->orbits.size(); ++i) {
            const auto& [points, color] = snap->orbits[i];
            stored_points += points.size();

            if (ui_config::draw_orbits && i < orbit_limit &&
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

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##DebugOverlay", nullptr, flags)) {
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

            const auto row = [](const char* label, const std::string& value) {
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

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoResize;

    constexpr auto margin = static_cast<float>(config::WINDOW_MARGIN);

    constexpr float width = margin * 0.25f + config::GRID_SPACING;
    constexpr float available_height = config::WINDOW_HEIGHT - 2.0f * margin;

    const float rows_height = visible_count == 0
        ? 0.0f
        : font_size + static_cast<float>(visible_count - 1) * spacing;

    const float desired_height = ImGui::GetFrameHeight() + 2.0f * padding_y + rows_height;

    // Position & size
    ImGui::SetNextWindowPos(
        {
            static_cast<float>(config::WINDOW_WIDTH) - margin,
            margin
        },
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f)
    );

    ImGui::SetNextWindowSize({width, std::min(desired_height, available_height)});

    // Collapsing logic
    if (ui_config::legend_collapse_requested) {
        ImGui::SetNextWindowCollapsed(ui_config::legend_collapsed);
        ui_config::legend_collapse_requested = false;
    }

    const bool window_expanded = ImGui::Begin("Legend", nullptr, flags);

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

            const auto [mx, my] = ImGui::GetMousePos();

            const double dx = mx - center.x;
            const double dy = my - center.y;

            if (dx * dx + dy * dy <= radius * radius) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (body->rendering_visible) {
                        ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .disable_rendering = true});
                    } else {
                        ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .enable_rendering = true});
                    }
                }

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

                if (ImGui::BeginTable("PositionVelocity", 2)) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Position");
                    ImGui::Indent(15.0f);
                    if (imgui_input(px, displayed_position.x, i, "x", "x##position", 4, "m")) ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .px = px.val});
                    if (imgui_input(py, displayed_position.y, i, "y", "y##position", 4, "m")) ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .py = py.val});
                    if (imgui_input(pz, displayed_position.z, i, "z", "z##position", 4, "m")) ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .pz = pz.val});
                    ImGui::Unindent(15.0f);

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("Velocity");
                    ImGui::Indent(15.0f);

                    if (imgui_input(vx, displayed_velocity.x, i, "x", "x##velocity", 4, "m/s")) ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .vx = vx.val});
                    if (imgui_input(vy, displayed_velocity.y, i, "y", "y##velocity", 4, "m/s")) ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .vy = vy.val});
                    if (imgui_input(vz, displayed_velocity.z, i, "z", "z##velocity", 4, "m/s")) ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .reference_index = reference_index, .vz = vz.val});
                    ImGui::Unindent(15.0f);

                    ImGui::EndTable();
                }

                ImGui::Spacing();

                if (imgui_input(mass, body->mass, i, "Mass", "mass", 6, "kg")) ui_data::update_body_command_queue.push(ui_data::UpdateBodyCommand{.i = i, .mass = mass.val});

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

                ImGui::Text("Camera");

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

                ImGui::Text("Coordinates");

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
    const double speed = seconds > 0.0 ? years / seconds : 0.0;

    const std::size_t center_i = config::center_celestial_body_index.load();
    const str center_name = center_i < snap->bodies.size() && snap->bodies[center_i] ? snap->bodies[center_i]->name : "None";
    const str target_speed = runtime_config::target_simulation_speed > 0.0 ? std::format("{:.2f} yr/s", runtime_config::target_simulation_speed) : "Unlimited";

    const bool running = config::timer.is_running();
    const bool can_resume = runtime_config::target_total_simulation_time < 0.0 || runtime_config::target_total_simulation_time > simulated_years();

    imgui_style_setup(dark_mode);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_TextDisabled, dark_mode ? IM_COL32(170, 170, 170, 255) : IM_COL32(100, 100, 100, 255));

    constexpr float toolbar_height = config::WINDOW_MARGIN - 16.0f;
    constexpr float toolbar_width = config::WINDOW_WIDTH - 2.0f * config::WINDOW_MARGIN;

    ImGui::SetNextWindowPos(ImVec2(config::WINDOW_MARGIN, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbar_width, toolbar_height), ImGuiCond_Always);

    if (ImGui::Begin("##SimulationStats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
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
            ImGui::TableNextRow(ImGuiTableRowFlags_None, toolbar_height / 2);

            ImGui::TableSetColumnIndex(0);
            stat("TIME", std::format("{:.2f} yr", years));

            ImGui::TableSetColumnIndex(1);
            stat("AVERAGE SPEED", std::format("{:.2f} yr/s", speed));

            ImGui::TableSetColumnIndex(2);
            stat("STEP", runtime_config::time_step_string);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, toolbar_height / 2);

            ImGui::TableSetColumnIndex(0);
            stat("RUN TIME", std::format("{:.2f} s", seconds));

            ImGui::TableSetColumnIndex(1);
            stat("TARGET", target_speed);

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
                ImVec2(190.0f, 0.0f),
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

            if (ImGui::Button("Trails", ImVec2(-1.0f, 0.0f))) {
                ImGui::OpenPopup("##TrailOptions");
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Trails: %s\nMoon trails: %s",
                    ui_config::draw_orbits ? "shown" : "hidden",
                    ui_config::draw_orbits && ui_config::draw_moon_orbits
                        ? "shown" : "hidden"
                );
            }

            // Override the toolbar's zero vertical item spacing for this popup
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));

            if (ImGui::BeginPopup("##TrailOptions")) {
                ImGui::Checkbox("Show orbits (O)", &ui_config::draw_orbits);

                ImGui::BeginDisabled(!ui_config::draw_orbits);
                ImGui::Checkbox("Include moons (M)", &ui_config::draw_moon_orbits);
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

            ImGui::BeginDisabled(!running && !can_resume);

            const bool clicked = ImGui::InvisibleButton(
                "##pause_resume",
                ImVec2(button_width, button_height)
            );

            ImGui::EndDisabled();

            if (clicked) {
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
