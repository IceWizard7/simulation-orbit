#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <imgui.h>
#include <implot.h>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include "graph_history.hpp"
#include "orbital_elements.hpp"
#include "simulation.hpp"
#include "time_units.hpp"
#include "ui.hpp"

namespace {
    enum class GraphQuantity {
        time, steps,
        position_x, position_y, position_z, distance,
        velocity_x, velocity_y, velocity_z, speed, radial_speed,
        acceleration_x, acceleration_y, acceleration_z, acceleration,
        mass, body_kinetic_energy, body_momentum, angular_momentum,
        semi_major_axis, semi_minor_axis, eccentricity, linear_eccentricity, orbital_period, inclination, // Keep this order! is_orbital_element relies on it
        energy, kinetic_energy, potential_energy, energy_drift, relative_energy_drift,
        momentum_x, momentum_y, momentum_z, momentum, momentum_drift,
        barycenter_drift
    };

    struct QuantityInfo {
        GraphQuantity quantity;
        const char* name;
        const char* format; // ImPlot tick format; this is where the unit lives
        bool per_body;
    };

    constexpr std::array QUANTITIES{
        QuantityInfo{GraphQuantity::time, "Time", "%.4g s", false},
        QuantityInfo{GraphQuantity::steps, "Steps", "%.0f", false},
        QuantityInfo{GraphQuantity::position_x, "Position x", "%.4g m", true},
        QuantityInfo{GraphQuantity::position_y, "Position y", "%.4g m", true},
        QuantityInfo{GraphQuantity::position_z, "Position z", "%.4g m", true},
        QuantityInfo{GraphQuantity::distance, "Distance", "%.4g m", true},
        QuantityInfo{GraphQuantity::velocity_x, "Velocity x", "%.4g m/s", true},
        QuantityInfo{GraphQuantity::velocity_y, "Velocity y", "%.4g m/s", true},
        QuantityInfo{GraphQuantity::velocity_z, "Velocity z", "%.4g m/s", true},
        QuantityInfo{GraphQuantity::speed, "Speed", "%.4g m/s", true},
        QuantityInfo{GraphQuantity::radial_speed, "Radial speed", "%.4g m/s", true},
        QuantityInfo{GraphQuantity::acceleration_x, "Acceleration x", "%.4g m/s^2", true},
        QuantityInfo{GraphQuantity::acceleration_y, "Acceleration y", "%.4g m/s^2", true},
        QuantityInfo{GraphQuantity::acceleration_z, "Acceleration z", "%.4g m/s^2", true},
        QuantityInfo{GraphQuantity::acceleration, "Acceleration", "%.4g m/s^2", true},
        QuantityInfo{GraphQuantity::mass, "Mass", "%.4g kg", true},
        QuantityInfo{GraphQuantity::body_kinetic_energy, "Body kinetic energy", "%.4g J", true},
        QuantityInfo{GraphQuantity::body_momentum, "Body momentum", "%.4g kg m/s", true},
        QuantityInfo{GraphQuantity::angular_momentum, "Spec. angular momentum", "%.6g m^2/s", true},
        QuantityInfo{GraphQuantity::semi_major_axis, "Semi-major axis a", "%.6g m", true},
        QuantityInfo{GraphQuantity::semi_minor_axis, "Semi-minor axis b", "%.6g m", true},
        QuantityInfo{GraphQuantity::eccentricity, "Eccentricity e", "%.6g", true},
        QuantityInfo{GraphQuantity::linear_eccentricity, "Linear eccentricity c", "%.6g m", true},
        QuantityInfo{GraphQuantity::orbital_period, "Orbital period T", "%.6g s", true},
        QuantityInfo{GraphQuantity::inclination, "Inclination i", "%.4g deg", true},
        QuantityInfo{GraphQuantity::energy, "Total energy", "%.6g J", false},
        QuantityInfo{GraphQuantity::kinetic_energy, "Total kinetic energy", "%.6g J", false},
        QuantityInfo{GraphQuantity::potential_energy, "Total potential energy", "%.6g J", false},
        QuantityInfo{GraphQuantity::energy_drift, "Energy drift", "%.4g J", false},
        QuantityInfo{GraphQuantity::relative_energy_drift, "Relative energy drift", "%.4g", false},
        QuantityInfo{GraphQuantity::momentum_x, "Momentum x", "%.4g kg m/s", false},
        QuantityInfo{GraphQuantity::momentum_y, "Momentum y", "%.4g kg m/s", false},
        QuantityInfo{GraphQuantity::momentum_z, "Momentum z", "%.4g kg m/s", false},
        QuantityInfo{GraphQuantity::momentum, "Momentum", "%.4g kg m/s", false},
        QuantityInfo{GraphQuantity::momentum_drift, "Momentum drift", "%.4g kg m/s", false},
        QuantityInfo{GraphQuantity::barycenter_drift, "Barycenter drift", "%.4g m", false},
    };

    // Compile time check (correct order and all used in QUANTITIES)
    static_assert([] {
        for (std::size_t i = 0; i < QUANTITIES.size(); i++) {
            if (static_cast<std::size_t>(QUANTITIES[i].quantity) != i) return false;
        }
        return true;
    }());

    const QuantityInfo& info(const GraphQuantity quantity) {
        return QUANTITIES[static_cast<std::size_t>(quantity)];
    }

    bool is_position(const GraphQuantity q) {
        return q == GraphQuantity::position_x || q == GraphQuantity::position_y || q == GraphQuantity::position_z;
    }

    bool is_orbital_element(const GraphQuantity q) {
        return q >= GraphQuantity::semi_major_axis && q <= GraphQuantity::inclination;
    }

    double evaluate(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphSample& s, const GraphQuantity quantity, const std::size_t body, const std::optional<std::size_t>& reference, const bool merge_moons) {
        constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

        if (!snap) return NaN;

        const Vec3 position = reference ? s.position[body] - s.position[*reference] : s.position[body];
        const Vec3 velocity = reference ? s.velocity[body] - s.velocity[*reference] : s.velocity[body];
        const Vec3 acceleration = reference ? s.acceleration[body] - s.acceleration[*reference] : s.acceleration[body];

        // Orbital elements need a central body: NaN in the inertial frame
        const auto orbital_element = [&](double OrbitalElements::* field) {
            if (!reference || !snap->bodies[body] || !snap->bodies[*reference]) return NaN;

            // Disabled bodies hold stale values, so they stay empty
            std::array<std::optional<PointMass>, NUM_CELESTIAL_BODIES> masses;
            for (std::size_t i = 0; i < masses.size(); i++) {
                if (snap->bodies[i]) masses[i] = PointMass{s.position[i], s.velocity[i], s.mass[i]};
            }

            const PointMass& ref = *masses[*reference];
            const PointMass orbiter = merge_moons
                ? system_barycenter(masses, body, *reference)
                : *masses[body];

            const double mu = (orbiter.mass + ref.mass) * config::GRAVITATIONAL_CONSTANT;
            const auto orbit = orbital_elements(orbiter.position - ref.position, orbiter.velocity - ref.velocity, mu);
            return orbit ? (*orbit).*field : NaN;
        };

        switch (quantity) {
            case GraphQuantity::time: return static_cast<double>(s.step) * runtime_config::time_step;
            case GraphQuantity::steps: return static_cast<double>(s.step);
            case GraphQuantity::position_x: return position.x;
            case GraphQuantity::position_y: return position.y;
            case GraphQuantity::position_z: return position.z;
            case GraphQuantity::distance: return position.length();
            case GraphQuantity::velocity_x: return velocity.x;
            case GraphQuantity::velocity_y: return velocity.y;
            case GraphQuantity::velocity_z: return velocity.z;
            case GraphQuantity::speed: return velocity.length();
            case GraphQuantity::radial_speed: return position.dot(velocity) / position.length();
            case GraphQuantity::acceleration_x: return acceleration.x;
            case GraphQuantity::acceleration_y: return acceleration.y;
            case GraphQuantity::acceleration_z: return acceleration.z;
            case GraphQuantity::acceleration: return acceleration.length();
            case GraphQuantity::mass: return s.mass[body];
            case GraphQuantity::body_kinetic_energy: return 0.5 * s.mass[body] * velocity.dot(velocity);
            case GraphQuantity::body_momentum: return s.mass[body] * velocity.length();
            case GraphQuantity::angular_momentum: return Vec3{
                position.y * velocity.z - position.z * velocity.y,
                position.z * velocity.x - position.x * velocity.z,
                position.x * velocity.y - position.y * velocity.x
            }.length();
            case GraphQuantity::semi_major_axis: return orbital_element(&OrbitalElements::semi_major_axis);
            case GraphQuantity::semi_minor_axis: return orbital_element(&OrbitalElements::semi_minor_axis);
            case GraphQuantity::eccentricity: return orbital_element(&OrbitalElements::eccentricity);
            case GraphQuantity::linear_eccentricity: return orbital_element(&OrbitalElements::linear_eccentricity);
            case GraphQuantity::orbital_period: return orbital_element(&OrbitalElements::orbital_period);
            case GraphQuantity::inclination: return orbital_element(&OrbitalElements::inclination) * 180.0 / std::numbers::pi;;
            case GraphQuantity::energy: return s.energy;
            case GraphQuantity::kinetic_energy: return s.kinetic_energy;
            case GraphQuantity::potential_energy: return s.energy - s.kinetic_energy;
            case GraphQuantity::energy_drift: return s.energy_drift;
            case GraphQuantity::relative_energy_drift: return s.energy_drift / std::abs(s.energy - s.energy_drift);
            case GraphQuantity::momentum_x: return s.momentum.x;
            case GraphQuantity::momentum_y: return s.momentum.y;
            case GraphQuantity::momentum_z: return s.momentum.z;
            case GraphQuantity::momentum: return s.momentum.length();
            case GraphQuantity::momentum_drift: return s.momentum_drift.length();
            case GraphQuantity::barycenter_drift: return s.barycenter_valid ? s.barycenter_drift.length() : std::numeric_limits<double>::quiet_NaN();
        }

        return std::numeric_limits<double>::quiet_NaN();
    }

    struct GraphWindow {
        std::size_t id = 0;
        bool open = true;
        bool merge_moons = true;

        // Unbound
        std::size_t unbound_points = 0;
        bool currently_unbound = false;

        GraphQuantity x = GraphQuantity::position_x;
        GraphQuantity y = GraphQuantity::position_y;
        std::size_t body = 0;
        std::optional<std::size_t> reference;
        bool auto_fit = true;

        std::vector<double> xs, ys, ts;
        std::size_t next_sample = 0;
        std::uint64_t seen_generation = 0;
        std::size_t every_steps = 1;
        bool rebuild = true;
        bool fit_next_frame = false;
        bool plot_hovered = false; // from the previous frame
        ImPlotRange visible_x{0.0, 1.0}, visible_y{0.0, 1.0}; // from the previous frame
    };

    std::vector<GraphWindow> graph_windows;
    std::size_t next_graph_id = 1;

    bool is_unbound(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphSample& s, const GraphWindow& w) {
        if (!is_orbital_element(w.x) && !is_orbital_element(w.y)) return false;

        const auto value = [&](const GraphQuantity q) {
            return evaluate(snap, s, q, w.body, w.reference, w.merge_moons);
        };

        // e is still finite when unbound; NaN means no orbit at all (no reference, mass <= 0, r = 0)
        return !std::isfinite(value(GraphQuantity::semi_major_axis)) && std::isfinite(value(GraphQuantity::eccentricity));
    }

    void sync(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, GraphWindow &w) {
        if (!snap) return;

        simulation::graph_history.read([&](const std::span<const GraphSample> samples, const std::uint64_t generation, const std::size_t every_steps) {
            w.every_steps = every_steps;

            if (w.rebuild || w.seen_generation != generation || w.merge_moons) {
                w.fit_next_frame |= w.rebuild; // New quantity/body: The old zoom is meaningless
                w.ts.clear();
                w.xs.clear();
                w.ys.clear();
                w.next_sample = 0;
                w.unbound_points = 0;
                w.currently_unbound = false;
                w.seen_generation = generation;
                w.rebuild = false;
            }

            constexpr double gap = std::numeric_limits<double>::quiet_NaN();

            for (; w.next_sample < samples.size(); w.next_sample++) {
                const GraphSample& s = samples[w.next_sample];

                if (s.starts_segment && !w.xs.empty()) {
                    w.xs.push_back(gap);
                    w.ys.push_back(gap);
                    w.ts.push_back(gap);
                }

                w.xs.push_back(evaluate(snap, s, w.x, w.body, w.reference, w.merge_moons));
                w.ys.push_back(evaluate(snap, s, w.y, w.body, w.reference, w.merge_moons));
                w.ts.push_back(evaluate(snap, s, GraphQuantity::time, w.body, w.reference, w.merge_moons));

                w.currently_unbound = is_unbound(snap, s, w);
                w.unbound_points += w.currently_unbound;
            }
        });
    }

    bool quantity_combo(const char* label, GraphQuantity& quantity) {
        bool changed = false;

        if (ImGui::BeginCombo(label, info(quantity).name, ImGuiComboFlags_HeightLarge)) {
            for (const QuantityInfo& item : QUANTITIES) {
                const bool selected = item.quantity == quantity;

                if (ImGui::Selectable(item.name, selected) && !selected) {
                    quantity = item.quantity;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    bool body_list(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const std::optional<std::size_t>& selected, const std::optional<std::size_t>& excluded, std::size_t& result) {
        bool changed = false;

        for (std::size_t i = 0; i < snap->bodies.size(); i++) {
            const auto& candidate = snap->bodies[i];
            if (!candidate || i == excluded) continue;

            ImGui::PushID(static_cast<int>(i));

            if (ui_helpers::body_selectable(candidate->name, selected == i, candidate->color)) {
                result = i;
                changed = true;
            }

            ImGui::PopID();
        }

        return changed;
    }

    str body_name(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const std::size_t i) {
        return snap->bodies[i] ? snap->bodies[i]->name : "N/A";
    }

    int format_time_tick(const double seconds, char* buffer, const int size, void* user_data) {
        const auto* unit = static_cast<const time_units::TimeUnit*>(user_data);
        return std::snprintf(buffer, static_cast<std::size_t>(size), "%.6g %s", seconds / unit->seconds, unit->suffix);
    }

    void setup_duration_axis(const ImAxis axis, const ImPlotRange& visible) {
        const time_units::TimeUnit& unit = time_units::choose(std::max(std::abs(visible.Min), std::abs(visible.Max)));
        ImPlot::SetupAxisFormat(axis, format_time_tick, const_cast<void*>(static_cast<const void*>(&unit)));
    }

    // ImPlot picks round ticks in seconds (ex. 1e7 s = 115.741 d), so place them at round values of the chosen unit
    void setup_time_axis(const ImAxis axis, const ImPlotRange& visible) {
        const double span = visible.Size();

        if (!std::isfinite(span) || span <= 0.0) {
            ImPlot::SetupAxisFormat(axis, info(GraphQuantity::time).format);
            return;
        }

        const time_units::TimeUnit& unit = time_units::choose(span);

        constexpr double TARGET_TICKS = 5.0;
        const double raw_step = span / unit.seconds / TARGET_TICKS;
        const double magnitude = std::pow(10.0, std::floor(std::log10(raw_step)));
        const double normalized = raw_step / magnitude;
        const double nice = normalized < 1.5 ? 1.0 : normalized < 3.5 ? 2.0 : normalized < 7.5 ? 5.0 : 10.0;
        const double step = nice * magnitude * unit.seconds;

        std::vector<double> ticks;

        for (double k = std::ceil(visible.Min / step); k * step <= visible.Max; k += 1.0) {
            ticks.push_back(k * step + 0.0); // + 0.0 turns -0.0 into 0.0
        }

        // The format must be set first: SetupAxisTicks formats its labels immediately
        ImPlot::SetupAxisFormat(axis, format_time_tick, const_cast<void*>(static_cast<const void*>(&unit)));

        if (!ticks.empty()) {
            ImPlot::SetupAxisTicks(axis, ticks.data(), static_cast<int>(ticks.size()));
        }
    }

    str format_value(const GraphQuantity q, const double value) {
        if (q == GraphQuantity::time || q == GraphQuantity::orbital_period) {
            return time_units::format_duration(value);
        }

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), info(q).format, value);
        return buffer;
    }

    // Moons that would currently merge with w.body
    std::vector<std::size_t> system_members(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphWindow& w) {
        std::vector<std::size_t> members;

        if (w.reference && snap->bodies[w.body] && snap->bodies[*w.reference]) {
            system_barycenter(ui_helpers::to_point_masses(*snap), w.body, *w.reference, &members);
        }

        return members;
    }

    void draw_controls(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, GraphWindow& w) {
        ImGui::TextUnformatted("X axis");
        ImGui::SetNextItemWidth(-FLT_MIN);
        w.rebuild |= quantity_combo("##x", w.x);

        ImGui::TextUnformatted("Y axis");
        ImGui::SetNextItemWidth(-FLT_MIN);
        w.rebuild |= quantity_combo("##y", w.y);

        ImGui::Separator();
        ImGui::BeginDisabled(!info(w.x).per_body && !info(w.y).per_body);

        ImGui::TextUnformatted("Body");
        ImGui::SetNextItemWidth(-FLT_MIN);

        if (ImGui::BeginCombo("##body", body_name(snap, w.body).c_str())) {
            if (body_list(snap, w.body, std::nullopt, w.body)) {
                if (w.reference == w.body) w.reference.reset();
                w.rebuild = true;
            }
            ImGui::EndCombo();
        }

        ImGui::TextUnformatted("Relative to");
        ImGui::SetNextItemWidth(-FLT_MIN);

        const str reference_name = w.reference ? body_name(snap, *w.reference) : "None (inertial)";

        if (ImGui::BeginCombo("##reference", reference_name.c_str())) {
            if (ImGui::Selectable("None (inertial)", !w.reference)) {
                w.reference.reset();
                w.rebuild = true;
            }

            ImGui::Separator();

            std::size_t chosen = 0;
            if (body_list(snap, w.reference, w.body, chosen)) {
                w.reference = chosen;
                w.rebuild = true;
            }
            ImGui::EndCombo();
        }

        ImGui::EndDisabled();

        if (!w.reference && (is_orbital_element(w.x) || is_orbital_element(w.y))) {
            ImGui::TextDisabled("Orbital elements need\na \"Relative to\" body");
        }

        if ((is_orbital_element(w.x) || is_orbital_element(w.y)) && !system_members(snap, w).empty()) {
            w.rebuild |= ImGui::Checkbox("Include moons", &w.merge_moons);
        }

        ImGui::Separator();

        ImGui::Checkbox("Auto-fit axes", &w.auto_fit);

        if (ImGui::Button("Clear all graphs", ImVec2(-FLT_MIN, 0.0f))) {
            ui_data::simulation_command_queue.push(ui_data::ClearGraphs{});

            // A manual zoom would otherwise keep pointing at the old region
            for (GraphWindow& other : graph_windows) {
                other.auto_fit = true;
            }
        }

        ImGui::TextDisabled("%zu points", w.xs.size());
        if (w.unbound_points > 0) {
            ImGui::TextDisabled(w.currently_unbound ? "Unbound now (%zu points)" : "Unbound for %zu points", w.unbound_points);
        }
        ImGui::TextDisabled("Spacing: %s", format_value(GraphQuantity::time, static_cast<double>(w.every_steps) * runtime_config::time_step).c_str());
    }

    str axis_label(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphWindow& w, const GraphQuantity q) {
        const QuantityInfo& item = info(q);

        if (!item.per_body) {
            return q == GraphQuantity::time || q == GraphQuantity::steps ? item.name : std::format("{} (inertial frame)", item.name);
        }

        if (q == GraphQuantity::mass) return std::format("{}: {}", item.name, body_name(snap, w.body));

        const bool merged = is_orbital_element(q) && w.merge_moons && !system_members(snap, w).empty();
        const str name = merged ? std::format("{} system", body_name(snap, w.body)) : body_name(snap, w.body);
        const str frame = w.reference ? std::format("rel. {}", body_name(snap, *w.reference)) : "inertial";
        return std::format("{}: {} ({})", item.name, name, frame);
    }

    ImVec4 graph_color(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphWindow& w) {
        const bool shows_body = info(w.x).per_body || info(w.y).per_body;

        if (shows_body && snap->bodies[w.body] && snap->bodies[w.body]->color) {
            return ImGui::ColorConvertU32ToFloat4(ui_helpers::to_imgui_color(*snap->bodies[w.body]->color));
        }

        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }

    constexpr float CURRENT_DOT_RADIUS = 3.0f;
    constexpr float SPATIAL_DOT_SCALE = 2.0f;

    bool is_time_axis(const GraphQuantity q) {
        return q == GraphQuantity::time || q == GraphQuantity::steps;
    }

    // Time graphs: y = f(x), so only the cursor's x matters
    // Other graphs can have several y per x (ex. an orbit), so take the closest point on screen
    std::optional<std::size_t> find_hovered_sample(const GraphWindow& w, const ImPlotPoint& mouse) {
        const ImVec2 mouse_px = ImPlot::PlotToPixels(mouse);
        const bool x_only = is_time_axis(w.x);

        std::optional<std::size_t> best;
        double best_distance = std::numeric_limits<double>::infinity();

        for (std::size_t i = 0; i < w.xs.size(); i++) {
            if (!std::isfinite(w.xs[i]) || !std::isfinite(w.ys[i])) continue;

            double distance;

            if (x_only) {
                distance = std::abs(w.xs[i] - mouse.x);
            } else {
                const ImVec2 point_px = ImPlot::PlotToPixels(w.xs[i], w.ys[i]);
                const double dx = point_px.x - mouse_px.x;
                const double dy = point_px.y - mouse_px.y;
                distance = dx * dx + dy * dy;
            }

            if (distance < best_distance) {
                best_distance = distance;
                best = i;
            }
        }

        return best;
    }

    void draw_hover(const GraphWindow& w, const ImU32 line_color) {
        const auto index = find_hovered_sample(w, ImPlot::GetPlotMousePos());
        if (!index) return;

        const ImVec2 point = ImPlot::PlotToPixels(w.xs[*index], w.ys[*index]);
        const ImVec2 plot_min = ImPlot::GetPlotPos();
        const ImVec2 plot_size = ImPlot::GetPlotSize();
        const ImU32 guide_color = ImGui::GetColorU32(ImGuiCol_Text, 0.45f);

        ImPlot::PushPlotClipRect();

        ImDrawList* draw_list = ImPlot::GetPlotDrawList();
        draw_list->AddLine(ImVec2(point.x, plot_min.y), ImVec2(point.x, plot_min.y + plot_size.y), guide_color);
        draw_list->AddLine(ImVec2(plot_min.x, point.y), ImVec2(plot_min.x + plot_size.x, point.y), guide_color);
        draw_list->AddCircleFilled(point, 4.0f, line_color);
        draw_list->AddCircle(point, 6.0f, guide_color);

        ImPlot::PopPlotClipRect();

        if (ImGui::BeginTooltip()) {
            ImGui::Text("%s: %s", info(w.x).name, format_value(w.x, w.xs[*index]).c_str());
            ImGui::Text("%s: %s", info(w.y).name, format_value(w.y, w.ys[*index]).c_str());

            if (w.x != GraphQuantity::time && w.y != GraphQuantity::time) {
                ImGui::TextDisabled("t = %s", format_value(GraphQuantity::time, w.ts[*index]).c_str());
            }

            ImGui::EndTooltip();
        }
    }


    void draw_current_dot(const GraphWindow& w, const ImU32 line_color) {
        const bool spatial = is_position(w.x) && is_position(w.y);
        const float radius = CURRENT_DOT_RADIUS * (spatial ? SPATIAL_DOT_SCALE : 1.0f);

        if (w.xs.empty() || !std::isfinite(w.xs.back()) || !std::isfinite(w.ys.back())) return;

        ImPlot::PushPlotClipRect();
        ImPlot::GetPlotDrawList()->AddCircleFilled(ImPlot::PlotToPixels(w.xs.back(), w.ys.back()), radius, line_color);
        ImPlot::PopPlotClipRect();
    }

    void draw_plot(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, GraphWindow& w) {
        // Auto-fit locks the axes, so the first zoom or drag has to switch it off before BeginPlot
        const bool user_moved_view = ImGui::GetIO().MouseWheel != 0.0f
                                  || ImGui::IsMouseDragging(ImGuiMouseButton_Left)
                                  || ImGui::IsMouseDragging(ImGuiMouseButton_Right);

        if (w.plot_hovered && user_moved_view) w.auto_fit = false;

        if (w.fit_next_frame) {
            ImPlot::SetNextAxesToFit();
            w.fit_next_frame = false;
        }

        const bool spatial = is_position(w.x) && is_position(w.y);

        const ImPlotFlags plot_flags = ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | (spatial ? ImPlotFlags_Equal : ImPlotFlags_None);
        const ImPlotAxisFlags axis_flags = ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch | (w.auto_fit ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None);

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(6.0f, 6.0f));
        ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(3.0f, 3.0f));
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.05f, 0.05f));

        w.plot_hovered = false;

        if (ImPlot::BeginPlot("##plot", ImVec2(-1.0f, -1.0f), plot_flags)) {
            const str x_label = axis_label(snap, w, w.x);
            const str y_label = axis_label(snap, w, w.y);

            ImPlot::SetupAxes(x_label.c_str(), y_label.c_str(), axis_flags, axis_flags);
            if (w.x == GraphQuantity::time) setup_time_axis(ImAxis_X1, w.visible_x);
            else if (w.x == GraphQuantity::orbital_period) setup_duration_axis(ImAxis_X1, w.visible_x);
            else ImPlot::SetupAxisFormat(ImAxis_X1, info(w.x).format);

            if (w.y == GraphQuantity::time) setup_time_axis(ImAxis_Y1, w.visible_y);
            else if (w.y == GraphQuantity::orbital_period) setup_duration_axis(ImAxis_Y1, w.visible_y);
            else ImPlot::SetupAxisFormat(ImAxis_Y1, info(w.y).format);

            const ImVec4 color = graph_color(snap, w);

            ImPlotSpec spec;
            spec.LineColor = color;

            ImPlot::PlotLine("##series", w.xs.data(), w.ys.data(), static_cast<int>(w.xs.size()), spec);

            const ImU32 line_color = ImGui::GetColorU32(color);

            const ImPlotRect limits = ImPlot::GetPlotLimits();
            w.visible_x = limits.X;
            w.visible_y = limits.Y;

            draw_current_dot(w, line_color);

            if (ImPlot::IsPlotHovered()) draw_hover(w, line_color);

            w.plot_hovered = ImPlot::IsPlotHovered() || ImPlot::IsAxisHovered(ImAxis_X1) || ImPlot::IsAxisHovered(ImAxis_Y1);

            ImPlot::EndPlot();
        }

        ImPlot::PopStyleVar(3);
    }

    void draw_graph_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, GraphWindow& w) {
        constexpr ImVec2 WINDOW_SIZE(620.0f, 340.0f);
        constexpr float CONTROLS_WIDTH = 170.0f;

        ImGui::SetNextWindowSize(WINDOW_SIZE, ImGuiCond_FirstUseEver);

        // New windows would otherwise open exactly on top of each other
        const float cascade = 24.0f * static_cast<float>((w.id - 1) % 8);
        ImGui::SetNextWindowPos(ImVec2(80.0f + cascade, 120.0f + cascade), ImGuiCond_FirstUseEver);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
        ImGui::PushFont(config::imgui_ui_font, 13.0f);

        // ### keeps the ImGui ID stable if the visible title changes later
        const str title = std::format("Graph {}###graph_{}", w.id, w.id);

        if (ImGui::Begin(title.c_str(), &w.open,  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            if (ImGui::BeginChild("##controls", ImVec2(CONTROLS_WIDTH, 0.0f))) {
                draw_controls(snap, w);
            }
            ImGui::EndChild();

            ImGui::SameLine();
            draw_plot(snap, w);
        }

        ImGui::End();

        ImGui::PopFont();
        ImGui::PopStyleVar(3);
    }
}

void ui_helpers::open_graph_window(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const std::optional<std::size_t> body, const std::optional<std::size_t> reference) {
    if (!snap) return;

    constexpr std::size_t SUN = 0;
    constexpr std::size_t EARTH = 3;

    const auto populated = [&](const std::size_t i) {
        return i < snap->bodies.size() && snap->bodies[i].has_value();
    };

    GraphWindow w;

    if (body && populated(*body)) {
        w.body = *body;
    } else if (populated(EARTH)) {
        w.body = EARTH;
    } else {
        const auto first = std::ranges::find_if(snap->bodies, [](const auto& candidate) { return candidate.has_value(); });
        if (first == snap->bodies.end()) return;
        w.body = static_cast<std::size_t>(first - snap->bodies.begin());
    }

    if (reference && *reference != w.body && populated(*reference)) {
        w.reference = reference;
    } else {
        // A moon is most readable relative to its planet, everything else relative to the Sun
        for (const auto& system : simulation::planetary_systems) {
            if (std::ranges::find(system.moon_indices, w.body) != system.moon_indices.end() && populated(system.parent_index)) {
                w.reference = system.parent_index;
            }
        }

        if (!w.reference && w.body != SUN && populated(SUN)) w.reference = SUN;
    }

    w.merge_moons = ui_config::orbit_use_system_barycenter;
    w.id = next_graph_id++;
    graph_windows.push_back(std::move(w));
}

void ui_helpers::draw_graph_windows(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const bool dark_mode) {
    if (!snap || graph_windows.empty()) return;

    imgui_style_setup(dark_mode);

    for (GraphWindow& w : graph_windows) {
        sync(snap, w);
        draw_graph_window(snap, w);
    }

    imgui_style_cleanup();

    std::erase_if(graph_windows, [](const GraphWindow& w) { return !w.open; });
}
