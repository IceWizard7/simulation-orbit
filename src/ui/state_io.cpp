#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <raylib.h>
#include <string>
#include <utility>
#include <variant>
#include <nlohmann/json.hpp>

#include "time_units.hpp"
#include "ui.hpp"

#ifdef __EMSCRIPTEN__
#include <cstdlib>
#include <emscripten.h>

void ui_helpers::receive_picked_state_file() {
    // @formatter:off
    // Both strings are malloc'd by stringToNewUTF8 on the JS side; nullptr = nothing picked
    char* name = static_cast<char*>(EM_ASM_PTR({ return Module.pendingImport ? stringToNewUTF8(Module.pendingImport.name) : 0; }));
    if (!name) return;
    char* text = static_cast<char*>(EM_ASM_PTR({ const t = Module.pendingImport.text; Module.pendingImport = null; return stringToNewUTF8(t); }));

    import_state_text(text, name); // Same path as a dropped file: parse, gate check, push, status
    std::free(text);
    std::free(name);
    // @formatter:on
}

void ui_helpers::arm_state_file_picker(const bool armed) {
    // @formatter:off
    EM_ASM({ Module.filePickerArmed = !!$0; }, armed);
    // @formatter:on
}
#endif

// Keeps insertion order (plain JSON sorts keys alphabetically)
using ordered_json = nlohmann::ordered_json;

namespace {
    // Every accessor checks the type first: get<>() would throw, and Emscripten aborts on throw
    std::optional<double> number(const ordered_json& j) {
        if (!j.is_number()) return std::nullopt;
        const double value = j.get<double>();
        return std::isfinite(value) ? std::optional{value} : std::nullopt;
    }

    std::optional<Vec3> vec3(const ordered_json& j) {
        if (!j.is_array() || j.size() != 3) return std::nullopt;
        const auto x = number(j[0]), y = number(j[1]), z = number(j[2]);
        if (!x || !y || !z) return std::nullopt;
        return Vec3{*x, *y, *z};
    }

    std::optional<Color> color(const ordered_json& j) {
        if (!j.is_array() || j.size() != 3) return std::nullopt;

        Color c{0, 0, 0, 255};
        unsigned char* channels[] = {&c.r, &c.g, &c.b};

        for (std::size_t k = 0; k < 3; k++) {
            if (!j[k].is_number_integer()) return std::nullopt;
            const auto v = j[k].get<std::int64_t>();
            if (v < 0 || v > 255) return std::nullopt;
            *channels[k] = static_cast<unsigned char>(v);
        }

        return c;
    }

    // nullptr if missing (const operator[] on a missing key is undefined behavior)
    const ordered_json* member(const ordered_json& object, const char* key) {
        const auto it = object.find(key);
        return it != object.end() ? &*it : nullptr;
    }
}

str ui_helpers::state_json(const ui_data::RenderSnapshot& snap) {
    const auto vec3 = [](const Vec3& v) { return ordered_json::array({v.x, v.y, v.z}); };
    const double dt = runtime_config::time_step;

    ordered_json bodies = ordered_json::array();

    for (std::size_t i = 0; i < snap.bodies.size(); i++) {
        const auto& body = snap.bodies[i];
        if (!body) continue; // Not listed = disabled on import

        ordered_json oblateness = nullptr;
        if (body->j2 != 0.0) {
            oblateness = {
                {"j2", body->j2},
                {"reference_radius_m", body->j2_reference_radius_m},
                {"pole", vec3(body->pole_axis)},
            };
        }

        ordered_json color = nullptr;
        if (body->color) color = ordered_json::array({body->color->r, body->color->g, body->color->b}); // uint8 -> JSON integers

        ordered_json texture = nullptr;
        if (body->texture_id) {
            if (const auto& path = ui_config::body_textures[*body->texture_id].texture_path) texture = *path;
        }


        bodies.push_back({
            {"catalog_index", i < NUM_CATALOG_BODIES ? ordered_json(i) : ordered_json(nullptr)},
            {"name", body->name},
            {"gravitational_parameter", body->gravitational_parameter},
            {"mass", body->mass},
            {"position", vec3(body->absolute_position)},
            {"velocity", vec3(body->velocity)},
            {"radius_m", body->physical_radius_m},
            {"oblateness", std::move(oblateness)},
            {"color", std::move(color)},
            {"visible", body->rendering_visible},
            {"texture", std::move(texture)}
        });
    }

    const ordered_json root = {
        {"format", "simulation-orbit-state"},
        {"version", 1},
        {
            "units", {
                {"length", "m"}, {"velocity", "m/s"}, {"mass", "kg"},
                {"gravitational_parameter", "m^3/s^2"}, {"time", "s"},
            }
        },
        {"frame", "solar-system barycentric, J2000 ecliptic (stored inertial frame)"},
        {"initial_epoch", std::format("{:%Y-%m-%dT%H:%M:%S} TDB", std::chrono::sys_seconds{time_units::INITIAL_EPOCH})},
        {"simulated_seconds", static_cast<double>(snap.step) * dt},
        {"step", snap.step},
        {"time_step_s", dt},
        {
            "settings", {
                {"body_set", runtime_config::body_set_name()},
                {"interaction_set", runtime_config::interaction_set_name()},
                {"j2", runtime_config::use_j2},
            }
        },
        {"bodies", std::move(bodies)},
    };

    // Replace: invalid UTF-8 in a name becomes U+FFFD instead of throwing (Emscripten aborts on throw)
    return root.dump(2, ' ', false, ordered_json::error_handler_t::replace) + '\n';
}

std::variant<ui_data::ImportStateCommand, str> ui_helpers::parse_state_json(const str_view text, const str& filename) {
    const ordered_json root = ordered_json::parse(text, nullptr, false); // false: discarded value instead of an exception
    if (root.is_discarded() || !root.is_object()) return str("Not valid JSON");

    const ordered_json* format = member(root, "format");
    if (!format || !format->is_string() || format->get<str>() != "simulation-orbit-state") return str("Not a simulation-orbit state file");

    const ordered_json* version = member(root, "version");
    if (!version || !version->is_number_unsigned() || version->get<std::uint64_t>() != 1) return str("Unsupported version (expected 1)");

    ui_data::ImportStateCommand cmd;
    cmd.label = std::format("Imported: {}", filename);

    const ordered_json* seconds_json = member(root, "simulated_seconds");
    const auto seconds = seconds_json ? number(*seconds_json) : std::nullopt;
    if (!seconds || *seconds < 0) return str("simulated_seconds missing or invalid");

    const double rounded_steps = std::round(*seconds / runtime_config::time_step);
    if (!(rounded_steps >= 0.0 && rounded_steps < 0x1p64)) {
        return str("simulated_seconds exceeds the supported step range");
    }
    cmd.steps_simulated = static_cast<step_t>(rounded_steps);

    const ordered_json* bodies = member(root, "bodies");
    if (!bodies || !bodies->is_array()) return str("bodies missing or not an array"); // [] is valid: Empty scenario

    std::array<bool, NUM_CATALOG_BODIES> seen{};
    std::size_t custom = 0;

    for (std::size_t k = 0; k < bodies->size(); k++) {
        const ordered_json& b = (*bodies)[k];
        const auto fail = [&](const char* what) { return std::format("Body {}: {}", k + 1, what); };
        if (!b.is_object()) return fail("not an object");

        ui_data::ImportedBody body;

        // catalog_index: required; integer < NUM_CATALOG_BODIES (unique) or null
        const ordered_json* index = member(b, "catalog_index");
        if (!index) return fail("catalog_index missing");

        if (index->is_null()) {
            if (++custom > MAX_CUSTOM_BODIES) return fail("too many custom bodies");
        } else {
            if (!index->is_number_unsigned()) return fail("catalog_index invalid");
            const auto i = index->get<std::uint64_t>();
            if (i >= NUM_CATALOG_BODIES || seen[i]) return fail("catalog_index out of range or duplicate");
            seen[i] = true;
            body.catalog_index = static_cast<std::size_t>(i);
        }

        const ordered_json* name = member(b, "name");
        if (!name || !name->is_string()) return fail("name missing");
        body.name = name->get<str>();
        if (body.name.find_first_not_of(" \t\r\n") == str::npos) return fail("name empty");

        // GM is authoritative; mass is only a fallback for handwritten files
        if (const ordered_json* gm = member(b, "gravitational_parameter")) {
            const auto value = number(*gm);
            if (!value) return fail("gravitational_parameter invalid");
            body.gravitational_parameter = *value;
        } else if (const ordered_json* mass = member(b, "mass")) {
            const auto value = number(*mass);
            if (!value) return fail("mass invalid");
            body.gravitational_parameter = *value * config::GRAVITATIONAL_CONSTANT;
        } else {
            return fail("gravitational_parameter missing");
        }

        const ordered_json* position = member(b, "position");
        const ordered_json* velocity = member(b, "velocity");
        const auto p = position ? vec3(*position) : std::nullopt;
        const auto v = velocity ? vec3(*velocity) : std::nullopt;
        if (!p || !v) return fail("position/velocity missing or invalid");
        body.position = *p;
        body.velocity = *v;

        if (const ordered_json* radius = member(b, "radius_m")) {
            const auto value = number(*radius);
            if (!value || *value < 0) return fail("radius_m invalid");
            body.radius_m = *value;
        }

        if (const ordered_json* o = member(b, "oblateness")) {
            if (o->is_null()) {
                body.oblateness = std::optional<ui_data::Oblateness>{}; // Explicitly none
            } else {
                const ordered_json* j2 = o->is_object() ? member(*o, "j2") : nullptr;
                const ordered_json* radius = o->is_object() ? member(*o, "reference_radius_m") : nullptr;
                const ordered_json* pole = o->is_object() ? member(*o, "pole") : nullptr;

                const auto j2_value = j2 ? number(*j2) : std::nullopt;
                const auto radius_value = radius ? number(*radius) : std::nullopt;
                const auto pole_value = pole ? vec3(*pole) : std::nullopt;

                if (!j2_value || !radius_value || *radius_value <= 0 || !pole_value || pole_value->length() == 0.0) {
                    return fail("oblateness invalid");
                }

                body.oblateness = ui_data::Oblateness{*j2_value, *radius_value, *pole_value / pole_value->length()};
            }
        }

        if (const ordered_json* c = member(b, "color")) {
            if (c->is_null()) {
                body.color = std::optional<Color>{};
            } else {
                const auto value = color(*c);
                if (!value) return fail("color invalid (expected [r, g, b], 0..255)");
                body.color = *value;
            }
        }

        if (const ordered_json* visible = member(b, "visible")) {
            if (!visible->is_boolean()) return fail("visible invalid");
            body.visible = visible->get<bool>();
        }

        if (const ordered_json* t = member(b, "texture")) {
            if (t->is_null()) {
                body.texture_path = std::optional<str>{};
            } else {
                if (!t->is_string() || t->get_ref<const str&>().empty()) {
                    return fail("texture invalid (expected a path or null)");
                }
                body.texture_path = t->get<str>();
            }
        }

        cmd.bodies.push_back(std::move(body));
    }

    return cmd;
}

void ui_helpers::import_state_text(const str_view text, const str& filename) {
    auto parsed = parse_state_json(text, filename);

    if (const auto* error = std::get_if<str>(&parsed)) {
        ui_config::import_status = {std::format("Import failed: {}", *error), true};
        return;
    }

    // Same gate as the simulation thread, so the user sees why nothing happens
    if (runtime_config::use_simd || runtime_config::interaction_set != runtime_config::InteractionSet::full) {
        ui_config::import_status = {"Import needs --interaction-set full without --simd", true};
        return;
    }

    auto& cmd = std::get<ui_data::ImportStateCommand>(parsed);

    // Texture uploads need the GL context: Resolve paths here, the simulation thread only receives registry IDs
    std::size_t missing_textures = 0;
    for (auto& body : cmd.bodies) {
        if (!body.texture_path) continue; // Key missing: Keep the default

        if (!*body.texture_path) {
            body.texture_id = std::optional<std::size_t>{}; // Explicitly none
        } else if (const auto id = register_custom_texture_file(**body.texture_path)) {
            body.texture_id = id;
        } else {
            missing_textures++; // Keep the default rather than failing the whole import
        }
    }

    const std::size_t count = cmd.bodies.size(); // Read before the move empties it
    ui_data::simulation_command_queue.push(std::move(cmd));
    reset_editor_drafts();

    ui_config::import_status = {
        missing_textures == 0
            ? std::format("Imported {} bodies (paused; see Checkpoints)", count)
            : std::format("Imported {} bodies; {} texture(s) not found (default kept)", count, missing_textures),
        missing_textures != 0 // Shown as a warning
    };
}

void ui_helpers::import_state_file(const str& path) {
    char* text = LoadFileText(path.c_str());
    if (!text) {
        ui_config::import_status = {"Import failed: cannot read file", true};
        return;
    }

    const str contents = text; // Copy, so the raylib buffer can be freed right away
    UnloadFileText(text);
    import_state_text(contents, GetFileName(path.c_str()));
}
