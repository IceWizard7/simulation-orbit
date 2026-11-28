#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <imgui.h>
#include <implot.h>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <raylib.h>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "graph_history.hpp"
#include "hierarchy.hpp"
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

    enum class QuantitySelectionUse {
        unspecified, none, body_only, optional_reference, required_reference
    };

    struct QuantityInfo {
        GraphQuantity quantity;
        const char* name;
        const char* format; // ImPlot tick format; this is where the unit lives
        QuantitySelectionUse selection_use;

        bool uses_body() const {
            return selection_use != QuantitySelectionUse::none;
        }

        bool uses_reference() const {
            return selection_use == QuantitySelectionUse::optional_reference
                   || selection_use == QuantitySelectionUse::required_reference;
        }
    };

    constexpr std::array QUANTITIES{
        QuantityInfo{GraphQuantity::time, "Time", "%.4g s", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::steps, "Steps", "%.0f", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::position_x, "Position x", "%.4g m", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::position_y, "Position y", "%.4g m", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::position_z, "Position z", "%.4g m", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::distance, "Distance", "%.4g m", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::velocity_x, "Velocity x", "%.4g m/s", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::velocity_y, "Velocity y", "%.4g m/s", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::velocity_z, "Velocity z", "%.4g m/s", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::speed, "Speed", "%.4g m/s", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::radial_speed, "Radial speed", "%.4g m/s", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::acceleration_x, "Acceleration x", "%.4g m/s^2", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::acceleration_y, "Acceleration y", "%.4g m/s^2", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::acceleration_z, "Acceleration z", "%.4g m/s^2", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::acceleration, "Acceleration", "%.4g m/s^2", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::mass, "Mass", "%.4g kg", QuantitySelectionUse::body_only},
        QuantityInfo{GraphQuantity::body_kinetic_energy, "Body kinetic energy", "%.4g J", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::body_momentum, "Body momentum", "%.4g kg m/s", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::angular_momentum, "Spec. angular momentum", "%.6g m^2/s", QuantitySelectionUse::optional_reference},
        QuantityInfo{GraphQuantity::semi_major_axis, "Semi-major axis a", "%.6g m", QuantitySelectionUse::required_reference},
        QuantityInfo{GraphQuantity::semi_minor_axis, "Semi-minor axis b", "%.6g m", QuantitySelectionUse::required_reference},
        QuantityInfo{GraphQuantity::eccentricity, "Eccentricity e", "%.6g", QuantitySelectionUse::required_reference},
        QuantityInfo{GraphQuantity::linear_eccentricity, "Linear eccentricity c", "%.6g m", QuantitySelectionUse::required_reference},
        QuantityInfo{GraphQuantity::orbital_period, "Orbital period τ", "%.6g s", QuantitySelectionUse::required_reference},
        QuantityInfo{GraphQuantity::inclination, "Inclination i", "%.4g deg", QuantitySelectionUse::required_reference},
        QuantityInfo{GraphQuantity::energy, "Total energy", "%.6g J", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::kinetic_energy, "Total kinetic energy", "%.6g J", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::potential_energy, "Total potential energy", "%.6g J", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::energy_drift, "Energy drift", "%.4g J", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::relative_energy_drift, "Relative energy drift", "%.4g", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::momentum_x, "Momentum x", "%.4g kg m/s", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::momentum_y, "Momentum y", "%.4g kg m/s", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::momentum_z, "Momentum z", "%.4g kg m/s", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::momentum, "Momentum", "%.4g kg m/s", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::momentum_drift, "Momentum drift", "%.4g kg m/s", QuantitySelectionUse::none},
        QuantityInfo{GraphQuantity::barycenter_drift, "Barycenter drift", "%.4g m", QuantitySelectionUse::none},
    };

    // Compile time check (correct order and all used in QUANTITIES)
    static_assert([] {
        for (std::size_t i = 0; i < QUANTITIES.size(); i++) {
            if (static_cast<std::size_t>(QUANTITIES[i].quantity) != i) return false;
            if (QUANTITIES[i].selection_use == QuantitySelectionUse::unspecified) return false;
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

    bool same_body(const ui_data::RenderSnapshot& snap, const GraphSample& s, const std::size_t i) {
        const auto& current = snap.bodies[i];
        return current && current->id == s.body_id[i];
    }

    double evaluate(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphSample& s, const GraphQuantity quantity, const std::size_t body, const std::optional<std::size_t>& reference, const bool merge_moons) {
        constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

        if (!snap) return NaN;

        const auto& item = info(quantity);
        if (item.uses_body() && !same_body(*snap, s, body)) return NaN;
        if (item.selection_use == QuantitySelectionUse::required_reference && !reference) return NaN;
        if (item.uses_reference() && reference && !same_body(*snap, s, *reference)) return NaN;

        const Vec3 position = reference ? s.position[body] - s.position[*reference] : s.position[body];
        const Vec3 velocity = reference ? s.velocity[body] - s.velocity[*reference] : s.velocity[body];
        const Vec3 acceleration = reference ? s.acceleration[body] - s.acceleration[*reference] : s.acceleration[body];

        // Orbital elements need a central body: NaN in the inertial frame
        const auto orbital_element = [&](double OrbitalElements::* field) {
            if (!reference) return NaN;

            // Disabled bodies hold stale values, so they stay empty
            std::array<std::optional<PointMass>, NUM_CELESTIAL_BODIES> masses;
            for (std::size_t i = 0; i < masses.size(); i++) {
                if (s.body_id[i] != config::NO_BODY_ID) {
                    masses[i] = PointMass{s.position[i], s.velocity[i], s.mass[i]};
                }
            }

            if (!masses[body] || !masses[*reference]) return NaN;

            const PointMass& ref = *masses[*reference];
            const PointMass orbiter = merge_moons && can_merge(s.hierarchy, body, reference)
                                          ? system_barycenter(masses, s.hierarchy, body)
                                          : *masses[body];

            const double mu = (orbiter.mass + ref.mass) * config::GRAVITATIONAL_CONSTANT;
            const auto orbit = orbital_elements(orbiter.position - ref.position, orbiter.velocity - ref.velocity, mu);
            return orbit ? *orbit.*field : NaN;
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
            case GraphQuantity::inclination: return orbital_element(&OrbitalElements::inclination) * 180.0 / std::numbers::pi;
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

    // Osculating period of i around its current host; NaN for roots (bodies without a host), unbound orbits or invalid masses
    double own_period(const GraphSample& s, const std::size_t i) {
        const std::optional<std::size_t> host = s.hierarchy.host[i];
        if (!host) return std::numeric_limits<double>::quiet_NaN(); // root (ex. the Sun) or disabled slot

        const double mu = (s.mass[i] + s.mass[*host]) * config::GRAVITATIONAL_CONSTANT;
        const auto orbit = orbital_elements(
            s.position[i] - s.position[*host],
            s.velocity[i] - s.velocity[*host],
            mu
        );

        return orbit ? orbit->orbital_period : std::numeric_limits<double>::quiet_NaN();
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
        step_t every_steps = 1;
        bool rebuild = true;
        bool fit_next_frame = false;
        bool plot_hovered = false; // from the previous frame
        ImPlotRange visible_x{0.0, 1.0}, visible_y{0.0, 1.0}; // from the previous frame

        double fastest_period = std::numeric_limits<double>::quiet_NaN();
        std::size_t fastest_body = 0;

        ui_config::FileStatus export_status;
        bool capture_requested = false; // PNG: Set by the button, consumed after this frame is rendered
    };

    struct PendingCapture {
        Rectangle rect; // Logical (ImGui) coordinates
        str filename;
        std::size_t window_id;
    };

    std::optional<PendingCapture> pending_capture; // Renderer thread only; at most one per frame

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

    void sync(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, GraphWindow& w) {
        if (!snap) return;

        simulation::graph_history.read([&](const std::span<const GraphSample> samples, const std::uint64_t generation, const step_t every_steps) {
            w.every_steps = every_steps;

            if (w.rebuild || w.seen_generation != generation) {
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

            constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

            for (; w.next_sample < samples.size(); w.next_sample++) {
                const GraphSample& s = samples[w.next_sample];

                if (s.starts_segment && !w.xs.empty()) {
                    w.xs.push_back(NaN);
                    w.ys.push_back(NaN);
                    w.ts.push_back(NaN);
                }

                w.xs.push_back(evaluate(snap, s, w.x, w.body, w.reference, w.merge_moons));
                w.ys.push_back(evaluate(snap, s, w.y, w.body, w.reference, w.merge_moons));
                w.ts.push_back(evaluate(snap, s, GraphQuantity::time, w.body, w.reference, w.merge_moons));

                w.currently_unbound = is_unbound(snap, s, w);
                w.unbound_points += w.currently_unbound;
            }

            if (!samples.empty()) {
                const GraphSample& latest = samples.back();
                w.fastest_period = NaN;

                // Check one body; keep it if its period is the shortest so far
                const auto consider = [&](const std::size_t i) {
                    if (!same_body(*snap, latest, i)) return;

                    const double tau = own_period(latest, i);
                    if (std::isfinite(tau) && !(tau >= w.fastest_period)) {
                        // also true while fastest_period is NaN
                        w.fastest_period = tau;
                        w.fastest_body = i;
                    }
                };

                const auto is_global = [](const GraphQuantity q) {
                    return !info(q).uses_body() && q != GraphQuantity::time && q != GraphQuantity::steps;
                };

                if (is_global(w.x) || is_global(w.y)) {
                    for (std::size_t i = 0; i < snap->bodies.size(); i++) {
                        if (snap->bodies[i]) consider(i); // only populated (enabled) bodies
                    }
                } else {
                    consider(w.body);
                    if (w.reference) consider(*w.reference);
                }
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

    // Signature fixed by ImPlotFormatter (void*, not const void*): ImPlot passes user_data back unchanged
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    int format_time_tick(const double seconds, char* buffer, const int size, void* user_data) {
        const auto* unit = static_cast<const time_units::TimeUnit*>(user_data);
        return std::snprintf(buffer, static_cast<std::size_t>(size), "%.6g %s", seconds / unit->seconds, unit->suffix);
    }

    void setup_duration_axis(const ImAxis axis, const ImPlotRange& visible) {
        const time_units::TimeUnit& unit = time_units::choose(std::max(std::abs(visible.Min), std::abs(visible.Max)));
        ImPlot::SetupAxisFormat(axis, format_time_tick, const_cast<void*>(static_cast<const void*>(&unit)));
    }


    // CSV field quoting: Names chosen by the user may contain commas or quotes
    str csv_field(const str& text) {
        if (text.find_first_of(",\"\n") == str::npos) {
            return text;
        }
        str quoted = "\"";
        for (const char c : text) {
            if (c == '"') {
                quoted += '"';
            }
            quoted += c;
        }

        return quoted + '"';
    }

    // Moons that would currently merge with w.body
    std::vector<std::size_t> system_members(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphWindow& w) {
        return can_merge(snap->hierarchy, w.body, w.reference) ? members_of(snap->hierarchy, w.body) : std::vector<std::size_t>{};
    }

    str axis_label(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphWindow& w, const GraphQuantity q) {
        const QuantityInfo& item = info(q);

        if (!item.uses_body()) {
            return q == GraphQuantity::time || q == GraphQuantity::steps ? item.name : std::format("{} (inertial frame)", item.name);
        }

        if (item.selection_use == QuantitySelectionUse::body_only) return std::format("{}: {}", item.name, body_name(snap, w.body));

        const bool merged = is_orbital_element(q) && w.merge_moons && !system_members(snap, w).empty();
        const str name = merged ? std::format("{} system", body_name(snap, w.body)) : body_name(snap, w.body);
        const str frame = w.reference ? std::format("rel. {}", body_name(snap, *w.reference)) : "inertial";
        return std::format("{}: {} ({})", item.name, name, frame);
    }

    // One row per cached point; NaN separators (segment breaks) become a segment counter, other NaN values empty fields
    str graph_csv(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphWindow& w) {
        const auto field = [](const double v) { return std::isfinite(v) ? std::format("{}", v) : str{}; };

        str out = std::format("simulated_seconds,segment,{},{}\n",
                              csv_field(axis_label(snap, w, w.x)), csv_field(axis_label(snap, w, w.y)));

        std::size_t segment = 0;
        for (std::size_t i = 0; i < w.ts.size(); i++) {
            // Separator row, not a sample
            if (std::isnan(w.ts[i])) {
                segment++;
                continue;
            }

            out += std::format("{},{},{},{}\n", field(w.ts[i]), segment, field(w.xs[i]), field(w.ys[i]));
        }
        return out;
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

        // Above 2^53, double can no longer represent every possible consecutive number
        constexpr double MAX_EXACT_UNIT = 0x1p53;
        const double max_abs = std::max(std::abs(visible.Min), std::abs(visible.Max));

        if (step > 0.0 && std::isfinite(step) && max_abs / step < MAX_EXACT_UNIT) {
            for (double k = std::ceil(visible.Min / step); k * step <= visible.Max; k += 1.0) {
                ticks.push_back(k * step + 0.0); // Normalize -0.0 for the label
            }
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

    void draw_controls(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, GraphWindow& w) {
        ImGui::TextUnformatted("X axis");
        ImGui::SetNextItemWidth(-FLT_MIN);
        w.rebuild |= quantity_combo("##x", w.x);

        ImGui::TextUnformatted("Y axis");
        ImGui::SetNextItemWidth(-FLT_MIN);
        w.rebuild |= quantity_combo("##y", w.y);

        ImGui::Separator();
        ImGui::BeginDisabled(!info(w.x).uses_body() && !info(w.y).uses_body());

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

        if (ImGui::Button("CSV")) {
            const str filename = std::format("graph{}_step{}.csv", w.id, snap->step);
            const auto saved = ui_helpers::save_user_file(filename, graph_csv(snap, w), "text/csv");
            w.export_status = {saved ? std::format("Saved: {}", *saved) : "Export failed", !saved};
        }
        ImGui::SameLine();
        if (ImGui::Button("PNG")) w.capture_requested = true;

        if (!w.export_status.text.empty()) {
            ImGui::TextWrapped("%s", w.export_status.text.c_str()); // Narrow controls column: wrap the path
        }

        ImGui::TextDisabled("%zu points", w.xs.size());
        if (w.unbound_points > 0) {
            ImGui::TextDisabled(w.currently_unbound ? "Unbound now (%zu points)" : "Unbound for %zu points", w.unbound_points);
        }
        const double spacing = static_cast<double>(w.every_steps) * runtime_config::time_step;
        const auto spacing_str = format_value(GraphQuantity::time, spacing);
        ImGui::TextDisabled("Spacing: %s", spacing_str.c_str());

        if (std::isfinite(w.fastest_period)) {
            const double per_orbit = w.fastest_period / spacing;

            if (per_orbit < 10.0) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
                                   "Low-detail: %.1f samples/orbit", per_orbit
                );
                ImGui::SetItemTooltip(
                    "%s orbits every %s, but samples are %s apart.\nLines between samples cut corners.",
                    body_name(snap, w.fastest_body).c_str(), format_value(GraphQuantity::orbital_period, w.fastest_period).c_str(), spacing_str.c_str()
                );
            }
        }
    }

    ImVec4 graph_color(const std::shared_ptr<const ui_data::RenderSnapshot>& snap, const GraphWindow& w) {
        const bool shows_body = info(w.x).uses_body() || info(w.y).uses_body();

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

            // The whole plot frame incl. axes and labels is the last ImGui item after EndPlot
            if (w.capture_requested) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                pending_capture = PendingCapture{
                    .rect      = {min.x, min.y, max.x - min.x, max.y - min.y},
                    .filename  = std::format("graph{}_step{}.png", w.id, snap->step),
                    .window_id = w.id,
                };
                w.capture_requested = false;
            }
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

        if (ImGui::Begin(title.c_str(), &w.open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
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

    // NO_BODY_SELECTED (= size) is rejected by the range check
    const auto populated = [&](const std::size_t i) {
        return i < snap->bodies.size() && snap->bodies[i].has_value();
    };

    GraphWindow w;

    if (body && populated(*body)) {
        w.body = *body;
    } else if (populated(ui_config::followed_celestial_body_index)) {
        w.body = ui_config::followed_celestial_body_index;
    } else {
        const auto first = std::ranges::find_if(snap->bodies, [](const auto& candidate) { return candidate.has_value(); });
        if (first == snap->bodies.end()) return;
        w.body = static_cast<std::size_t>(first - snap->bodies.begin());
    }

    // Reference: explicit request, else whatever the body currently orbits (roots stay inertial)
    if (reference && *reference != w.body && populated(*reference)) {
        w.reference = reference;
    } else {
        w.reference = snap->hierarchy.host[w.body];
    }

    w.merge_moons = ui_config::orbit_use_system_barycenter; // Initially
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

void ui_helpers::forget_graph_body(const std::size_t i) {
    for (auto& w : graph_windows) {
        // Body is gone: Close the window
        if (w.body == i) w.open = false;

        // Body is gone: Reset reference of that window & rebuild
        if (w.reference == i) {
            w.reference = std::nullopt;
            w.rebuild = true;
        }
    }
}

std::optional<double> ui_helpers::create_draft_mu(const ui_data::RenderSnapshot& snap) {
    const auto& draft = ui_config::create_body_draft;
    if (!draft.reference_index || !snap.bodies[*draft.reference_index]) return std::nullopt;

    const double mu = config::GRAVITATIONAL_CONSTANT * (snap.bodies[*draft.reference_index]->mass + draft.mass);
    return std::isfinite(mu) && mu > 0.0 ? std::optional{mu} : std::nullopt;
}

double ui_helpers::placement_tau(const ui_data::RenderSnapshot& snap) {
    constexpr double FALLBACK_TAU = 86'400.0; // None / invalid: No host, arbitrary 1 day

    const auto mu = create_draft_mu(snap);
    const double r = ui_config::create_body_draft.position.length();
    if (!mu || !(r > 0.0)) return FALLBACK_TAU;

    const double tau = std::sqrt(r * r * r / *mu);
    return std::isfinite(tau) && tau > 0.0 ? tau : FALLBACK_TAU;
}

std::optional<ConicSectionPath> ui_helpers::create_ghost_conic_section_path(const ui_data::RenderSnapshot& snap) {
    if (!ui_config::show_create_body) return std::nullopt;

    const auto& draft = ui_config::create_body_draft;
    if (!draft.reference_index) return std::nullopt; // None: no host to orbit

    const std::optional<double> mu = create_draft_mu(snap);
    if (!mu) return std::nullopt;

    constexpr std::size_t CONIC_SAMPLES = 512;
    const double max_distance = 20.0 * draft.position.length(); // Hyperbola cut-off

    return sample_conic_section(draft.position, draft.velocity, *mu, CONIC_SAMPLES, max_distance);
}

str ui_helpers::full_state_csv(const std::shared_ptr<const ui_data::RenderSnapshot>& snap) {
    str out;
    const auto put = std::back_inserter(out);

    simulation::graph_history.read([&](const std::span<const GraphSample> samples, std::uint64_t, step_t) {
        // One column group per body ID that appears anywhere, in order of first appearance
        std::vector<std::uint64_t> ids;
        std::unordered_map<std::uint64_t, std::size_t> column;

        for (const GraphSample& s : samples) {
            for (const std::uint64_t id : s.body_id) {
                if (id != config::NO_BODY_ID && column.try_emplace(id, ids.size()).second) {
                    ids.push_back(id);
                }
            }
        }

        const auto name_of = [&](const std::uint64_t id) -> str {
            for (const auto& body : snap->bodies) {
                if (body && body->id == id) {
                    return body->name;
                }
            }
            return std::format("deleted_{}", id);
        };

        // CSV global column lines
        out += "step,simulated_seconds,segment";

        // CSV column lines for each body
        for (const std::uint64_t id : ids) {
            const str name = name_of(id);
            for (const char* field : {"position_x", "position_y", "position_z", "velocity_x", "velocity_y", "velocity_z", "mass"}) {
                out += ',';
                out += csv_field(std::format("{}_{}", name, field));
            }
        }
        out += '\n';

        std::size_t segment = 0;
        std::vector<std::optional<std::size_t>> slot(ids.size());

        for (std::size_t k = 0; k < samples.size(); k++) {
            const GraphSample& s = samples[k];
            if (s.starts_segment && k > 0) segment++;

            std::ranges::fill(slot, std::nullopt);
            for (std::size_t i = 0; i < s.body_id.size(); i++) {
                if (s.body_id[i] != config::NO_BODY_ID) {
                    slot[column.at(s.body_id[i])] = i;
                }
            }

            std::format_to(put, "{},{},{}", s.step, static_cast<double>(s.step) * runtime_config::time_step, segment);
            for (const auto& i : slot) {
                // Disabled slot, empty data
                if (!i) {
                    out += ",,,,,,,";
                    continue;
                }

                const Vec3& p = s.position[*i];
                const Vec3& v = s.velocity[*i];

                std::format_to(put, ",{},{},{},{},{},{},{}", p.x, p.y, p.z, v.x, v.y, v.z, s.mass[*i]);
            }
            out += '\n';
        }
    });

    return out;
}

void ui_helpers::capture_pending_graph() {
    if (!pending_capture) return;
    const PendingCapture capture = std::move(*pending_capture);
    pending_capture.reset();

    // Back buffer of this frame, in framebuffer pixels (Retina/web HiDPI: Larger than the logical window)
    Image image = LoadImageFromScreen();
    const float sx = static_cast<float>(image.width) / static_cast<float>(GetScreenWidth());
    const float sy = static_cast<float>(image.height) / static_cast<float>(GetScreenHeight());

    ImageCrop(
        &image,
        {
            std::round(capture.rect.x * sx), std::round(capture.rect.y * sy),
            std::round(capture.rect.width * sx), std::round(capture.rect.height * sy)
        }
    );

    int size = 0;
    unsigned char* png = ExportImageToMemory(image, ".png", &size);
    UnloadImage(image);

    std::optional<str> saved;
    if (png) {
        saved = save_user_file(capture.filename, str_view(reinterpret_cast<const char*>(png), static_cast<std::size_t>(size)), "image/png");
        MemFree(png);
    }

    // The window may have been closed in the meantime
    for (GraphWindow& w : graph_windows) {
        if (w.id == capture.window_id) {
            w.export_status = {saved ? std::format("Saved: {}", *saved) : "PNG export failed", !saved};
        }
    }
}
