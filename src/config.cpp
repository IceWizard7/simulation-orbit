#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <regex>
#include <stdexcept>
#include <vector>

namespace runtime_config {
    str invocation_command;
    bool exit_immediately = false;
    bool headless = false;
    int time_step = 900;
    double half_dt_squared = 0.5 * time_step * time_step;
    double half_dt = 0.5 * time_step;
    str time_step_string = "15 mins";

    double target_total_simulation_time = -1;
    double target_simulation_speed = -1;
    double target_steps_per_second = target_simulation_speed * config::SECONDS_PER_YEAR / time_step;
    std::optional<std::size_t> target_steps = std::nullopt;

    std::optional<std::filesystem::path> csv_path = std::nullopt;
    std::optional<int> sample_csv_data_every_seconds = std::nullopt;
    std::optional<std::size_t> sample_csv_data_every_steps = std::nullopt;
    bool csv_live = false;

    bool use_j2 = false;

    auto body_set = BodySet::dwarf;

    int enabled_celestial_bodies;
}

const char* runtime_config::body_set_name() {
    switch (body_set) {
        case BodySet::all: return "all";
        case BodySet::planets: return "planets";
        case BodySet::dwarf: return "dwarf";
        case BodySet::planet_systems: return "planet-systems";
        default: return "unknown";
    }
}

void runtime_config::update_body_set(CelestialBody (&celestial_bodies)[NUM_CELESTIAL_BODIES]) {
    switch (body_set) {
        case BodySet::all:
            for (auto & celestial_body : celestial_bodies) {
                celestial_body.enabled = true;
            }
            break;
        case BodySet::planets:
            for (int j = 0; j < NUM_CELESTIAL_BODIES; j++) {
                if (j < NUM_PLANETS) celestial_bodies[j].enabled = true;
                else celestial_bodies[j].enabled = false;
            }
            break;
        case runtime_config::BodySet::dwarf:
            for (int j = 0; j < NUM_CELESTIAL_BODIES; j++) {
                if (j < NUM_PLANETS + NUM_DWARF_PLANETS) celestial_bodies[j].enabled = true;
                else celestial_bodies[j].enabled = false;
            }
            break;
        case runtime_config::BodySet::planet_systems:
            for (int j = 0; j < NUM_CELESTIAL_BODIES; j++) {
                celestial_bodies[j].enabled = j < NUM_PLANETS + NUM_DWARF_PLANETS;
            }
            break;
    }
}


runtime_config::ParseRes<double> runtime_config::parse_double(const str& argument_name, const int i, const int argc, char* argv[]) {
    if (i + 1 >= argc) {
        std::cerr << std::format("Error: {} requires a number following it.\n", argument_name);
        return {0, 1};
    }

    double val = 0;

    try {
        std::size_t parsed_characters = 0;
        val = std::stod(argv[i + 1], &parsed_characters);
        if (parsed_characters != str(argv[i + 1]).size() || !std::isfinite(val)) {
            throw std::invalid_argument("not a finite number");
        }
    } catch (const std::exception&) {
        std::cerr << std::format("Error: {} is an invalid value for {}.\n", argv[i + 1], argument_name);
        return {0, 1};
    }

    return {val, 0};
}

runtime_config::ParseRes<int> runtime_config::parse_int(const str& argument_name, const int i, const int argc, char* argv[]) {
    if (i + 1 >= argc) {
        std::cerr << std::format("Error: {} requires a number following it.\n", argument_name);
        return {0, 1};
    }

    int val = 0;

    try {
        std::size_t parsed_characters = 0;
        val = std::stoi(argv[i + 1], &parsed_characters);
        if (parsed_characters != str(argv[i + 1]).size()) {
            throw std::invalid_argument("not an integer");
        }
    } catch (const std::exception&) {
        std::cerr << std::format("Error: {} is an invalid value for {}.\n", argv[i + 1], argument_name);
        return {0, 1};
    }

    return {val, 0};
}

runtime_config::ParseRes<str> runtime_config::parse_str(const str& argument_name, const int i, const int argc, char* argv[]) {
    if (i + 1 >= argc) {
        std::cerr << std::format("Error: {} requires a string following it.\n", argument_name);
        return {"", 1};
    }

    return {argv[i + 1], 0};
}

void print_logo() {
    printf(R"(
         ██████╗ ██████╗ ██████╗ ██╗████████╗    ███████╗██╗███╗   ███╗██╗   ██╗██╗      █████╗ ████████╗██╗ ██████╗ ███╗   ██╗
        ██╔═══██╗██╔══██╗██╔══██╗██║╚══██╔══╝    ██╔════╝██║████╗ ████║██║   ██║██║     ██╔══██╗╚══██╔══╝██║██╔═══██╗████╗  ██║
        ██║   ██║██████╔╝██████╔╝██║   ██║       ███████╗██║██╔████╔██║██║   ██║██║     ███████║   ██║   ██║██║   ██║██╔██╗ ██║
        ██║   ██║██╔══██╗██╔══██╗██║   ██║       ╚════██║██║██║╚██╔╝██║██║   ██║██║     ██╔══██║   ██║   ██║██║   ██║██║╚██╗██║
        ╚██████╔╝██║  ██║██████╔╝██║   ██║       ███████║██║██║ ╚═╝ ██║╚██████╔╝███████╗██║  ██║   ██║   ██║╚██████╔╝██║ ╚████║
         ╚═════╝ ╚═╝  ╚═╝╚═════╝ ╚═╝   ╚═╝       ╚══════╝╚═╝╚═╝     ╚═╝ ╚═════╝ ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚═╝ ╚═════╝ ╚═╝  ╚═══╝

        )");
    printf("\n");
}

// POSIX-shell-quote -> fully copy-paste reproducible
static str shell_quote(const str& argument) {
    if (argument.empty()) {
        return "''";
    }

    const bool needs_quoting = std::ranges::any_of(argument, [](const char c) {
        const auto uc = static_cast<unsigned char>(c);
        return !(std::isalnum(uc) || Vector_Utils::contains(c, {'_', '-', '.', '/', '=', ':', '+', ',', '@', '%'}));
    });

    if (!needs_quoting) {
        return argument;
    }

    // Wrap in single quotes; turn any ' character into '\''
    str quoted = "'";
    for (const char c : argument) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

int runtime_config::parse_cli_args(const int argc, char* argv[], CelestialBody (&celestial_bodies)[NUM_CELESTIAL_BODIES]) {
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            invocation_command += ' ';
        }

        invocation_command += shell_quote(argv[i]);
    }

    if (argc >= 2 && (str(argv[1]) == "--help" || str(argv[1]) == "-h")) {
        print_logo();
        printf("    Options:\n");
        printf("    -v, --version                                        Show version\n");
        printf("    -h, --help                                           Show help\n");
        printf("        --headless                                       Run without raylib window (default: off)\n");
        printf("        --dt <seconds>                                   Configure time step (default: 900)\n");
        printf("        --years <years>                                  Configure total simulation time (default: unlimited, -1)\n");
        printf("        --csv <path>                                     Set CSV path for export of positions (default: off)\n");
        printf("        --sample-every-seconds <seconds>                 Set the physical sampling interval. Must be a multiple of --dt (default: off)\n");
        printf("        --csv-live                                       Stream CSV rows during simulation instead of writing at the end (default: off)\n");
        printf("        --j2                                             Enable planetary oblateness (J2) perturbation on satellites (default: off)\n");
        printf("        --body-set <all|planets|dwarf|planet-systems>    Enable only a subset of the available celestial bodies, planet-systems uses barycentric approximation (default: dwarf)\n");
        printf("        --disable-body <name>                            Disable a certain body; must come after --body-set (default: none explicitly disabled)\n");
        exit_immediately = true;
        return 0;
    }
    if (argc >= 2 && (str(argv[1]) == "--version" || str(argv[1]) == "-v")) {
        printf("v0.1.0\n");
        exit_immediately = true;
        return 0;
    }

    bool years_supplied = false;
    std::vector<int> explicitly_disabled_body_indices;

    body_set = BodySet::dwarf;
    update_body_set(celestial_bodies);

    for (int i = 1; i < argc; i++) {
        str arg = argv[i];

        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--dt") {
            auto [val, err] = parse_int("--dt", i, argc, argv);
            if (err != 0) return err;
            time_step = val;
            i++;
        } else if  (arg == "--years") {
            auto [val, err] = parse_double("--years", i, argc, argv);
            if (err != 0) return err;
            target_total_simulation_time = val;
            years_supplied = true;
            i++;
        } else if (arg == "--csv") {
            auto [val, err] = parse_str("--csv", i, argc, argv);
            if (err != 0) return err;
            csv_path = val;
            i++;
        } else if (arg == "--sample-every-seconds") {
            auto [val, err] = parse_int("--sample-every-seconds", i, argc, argv);
            if (err != 0) return err;
            sample_csv_data_every_seconds = val;
            i++;
        } else if (arg == "--csv-live") {
            csv_live = true;
        } else if (arg == "--j2") {
            use_j2 = true;
        } else if (arg == "--body-set") {
            auto [val, err] = parse_str("--body-set", i, argc, argv);
            if (err != 0) return err;
            explicitly_disabled_body_indices.clear();
            if (val == "all") {
                body_set = BodySet::all;
                update_body_set(celestial_bodies);
            } else if (val == "planets") {
                body_set = BodySet::planets;
                update_body_set(celestial_bodies);
            } else if (val == "dwarf") {
                body_set = BodySet::dwarf;
                update_body_set(celestial_bodies);
            } else if (val == "planet-systems") {
                body_set = BodySet::planet_systems;
                update_body_set(celestial_bodies);
            } else {
                std::cerr << std::format(
                    "Error: {} is an invalid value for --body-set. Pass \"all\", \"planets\", \"dwarf\" or \"planet-systems\" instead.\n",
                    val
                );
                return 1;
            }
            i++;
        } else if (arg == "--disable-body") {
            auto [val, err] = parse_str("--disable-body", i, argc, argv);
            if (err != 0) return err;

            std::optional<int> celestial_body_index;

            for (int j = 0; j < NUM_CELESTIAL_BODIES; j++) {
                const auto& celestial_body = celestial_bodies[j];
                if (celestial_body.name == val) {
                    celestial_body_index = j;
                    break;
                }
            }

            if (!celestial_body_index.has_value()) {
                std::cerr << std::format("Error: {} is an invalid value for --disable-body. Pass a valid name instead.\n", val);
                return 1;
            }

            celestial_bodies[*celestial_body_index].enabled = false;
            explicitly_disabled_body_indices.push_back(*celestial_body_index);

            i++;
        } else {
            std::cerr << std::format("Error: Unexpected argument {}.\n", arg);
            return 1;
        }
    }

    if (time_step <= 0) {
        std::cerr << "Error: --dt must be greater than zero.\n";
        return 1;
    }

    if (years_supplied && (!std::isfinite(target_total_simulation_time) || target_total_simulation_time <= 0.0)) {
        std::cerr << "Error: --years must be a finite number greater than zero.\n";
        return 1;
    }

    if (headless && !years_supplied) {
        std::cerr << "Error: --headless requires --years.\n";
        return 1;
    }

    if (csv_path.has_value() != sample_csv_data_every_seconds.has_value()) {
        std::cerr << "Error: --csv requires --sample-every-seconds (and vice-versa).\n";
        return 1;
    }

    if (csv_path.has_value() && csv_path->empty()) {
        std::cerr << "Error: --csv path must not be empty.\n";
        return 1;
    }

    if (csv_live && (!csv_path.has_value() || !sample_csv_data_every_seconds.has_value())) {
        std::cerr << "Error: --csv-live requires --csv and --sample-every-seconds.\n";
        return 1;
    }

    if (sample_csv_data_every_seconds.has_value() && *sample_csv_data_every_seconds <= 0) {
        std::cerr << "Error: --sample-every-seconds must be greater than zero.\n";
        return 1;
    }

    if (sample_csv_data_every_seconds.has_value() && *sample_csv_data_every_seconds % time_step != 0) {
        std::cerr << std::format("Error: --sample-every-seconds ({}) must be a multiple of --dt ({}).\n", *sample_csv_data_every_seconds, time_step);
        return 1;
    }

    if (body_set == BodySet::planet_systems && use_j2) {
        std::cerr << "Error: --j2 cannot be combined with --body-set planet-systems; "
                     "the barycentric approximation is a spherical point-mass monopole.\n";
        return 1;
    }

    if (body_set == BodySet::planet_systems) {
        for (const int index : explicitly_disabled_body_indices) {
            if (index < NUM_PLANETS + NUM_DWARF_PLANETS) continue;
            std::cerr << std::format(
                "Error: --disable-body {} cannot be combined with --body-set planet-systems; "
                "that moon is already represented inside its parent system.\n",
                celestial_bodies[index].name
            );
            return 1;
        }
    }

    target_steps = std::nullopt;
    if (years_supplied) {
        const double requested_steps = target_total_simulation_time * config::SECONDS_PER_YEAR / time_step;
        const double rounded_steps = std::round(requested_steps);
        const double integer_tolerance = 8.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(requested_steps));

        if (!std::isfinite(requested_steps) || rounded_steps < 1.0 || rounded_steps > static_cast<double>(std::numeric_limits<std::size_t>::max()) || std::abs(requested_steps - rounded_steps) > integer_tolerance) {
            std::cerr << std::format(
                "Error: --years ({}) is not an integer number of --dt ({}) steps.\n",
                target_total_simulation_time,
                time_step
            );
            return 1;
        }

        target_steps = static_cast<std::size_t>(rounded_steps);
    }

    sample_csv_data_every_steps = std::nullopt;
    if (sample_csv_data_every_seconds.has_value()) {
        sample_csv_data_every_steps = static_cast<std::size_t>(*sample_csv_data_every_seconds / time_step);
    }

    set_time_step_string();
    half_dt_squared = 0.5 * time_step * time_step;
    half_dt = 0.5 * time_step;
    target_steps_per_second = target_simulation_speed * config::SECONDS_PER_YEAR / time_step;

    enabled_celestial_bodies = 0;
    for (const auto& celestial_body : celestial_bodies) {
        if (celestial_body.enabled) enabled_celestial_bodies++;
    }

    if (enabled_celestial_bodies == 0) {
        std::cerr << "Error: At least one celestial body must remain enabled.\n";
        return 1;
    }

    return 0;
}


void runtime_config::set_time_step_string() {
    auto set_string = [](double val, const str& unit) {
        constexpr double epsilon = 1e-6;
        if (std::abs(val - std::round(val)) < epsilon) {
            // effectively ends in .00
            time_step_string = std::format("{} {}", static_cast<int>(val), unit);
        } else {
            time_step_string = std::format("{:.2f} {}", val, unit);
        }
    };

    const auto step = static_cast<double>(time_step);

    if (step < 60) {
        set_string(step, "secs");
    } else if (step < 3'600) {
        set_string(step / 60, "mins");
    } else if (step < 86'400) {
        set_string(step / 3'600, "hrs");
    } else {
        set_string(step / 86'400, "days");
    }
}
