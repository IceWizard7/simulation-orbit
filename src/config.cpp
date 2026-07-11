#include "config.hpp"

#include <iostream>

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

    std::optional<std::filesystem::path> csv_path = std::nullopt;
    std::optional<int> sample_csv_data_every_seconds = std::nullopt;
}


runtime_config::ParseRes<double> runtime_config::parse_double(const str& argument_name, const int i, const int argc, char* argv[]) {
    if (i + 1 >= argc) {
        std::cerr << std::format("Error: {} requires a number following it.\n", argument_name);
        return {0, 1};
    }

    double val = 0;

    try {
        val = std::stod(argv[i + 1]);
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
        val = std::stoi(argv[i + 1]);
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

int runtime_config::parse_cli_args(const int argc, char* argv[]) {
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            invocation_command += ' ';
        }

        invocation_command += argv[i];
    }

    if (argc >= 2 && (str(argv[1]) == "--help" || str(argv[1]) == "-h")) {
        print_logo();
        printf("    Options:\n");
        printf("    -v, --version                           Show version\n");
        printf("    -h, --help                              Show help\n");
        printf("        --headless                          Run without raylib window\n");
        printf("        --dt <seconds>                      Configure time step\n");
        printf("        --years <years>                     Configure total simulation time\n");
        printf("        --csv <path>                        Set CSV path for export of positions\n");
        printf("        --sample-every-seconds <seconds>    Set the physical sampling interval. Must be a multiple of --dt\n");
        exit_immediately = true;
        return 0;
    }
    if (argc >= 2 && (str(argv[1]) == "--version" || str(argv[1]) == "-v")) {
        printf("v0.1.0\n");
        exit_immediately = true;
        return 0;
    }

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
        } else {
            std::cerr << std::format("Error: Unexpected argument {}.\n", arg);
            return 1;
        }
    }

    if (headless && target_total_simulation_time <= 0) {
        std::cerr << std::format("Error: --headless requires positive --years.\n");
        return 1;
    }

    if (csv_path.has_value() != sample_csv_data_every_seconds.has_value()) {
        std::cerr << std::format("--csv requires --sample-every-seconds (and vice-versa).\n");
        return 1;
    }

    if (sample_csv_data_every_seconds.has_value() && *sample_csv_data_every_seconds % time_step != 0) {
        std::cerr << std::format("--sample_every_seconds ({}) must be a multiple of --dt ({}).\n", *sample_csv_data_every_seconds, time_step);
        return 1;
    }

    set_time_step_string();
    half_dt_squared = 0.5 * time_step * time_step;
    half_dt = 0.5 * time_step;

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

    if (time_step < 60) {
        set_string(time_step, "secs");
    } else if (60 <= time_step && time_step < 3'600) {
        set_string(time_step / 60, "mins");
    } else if (3'600 <= time_step && time_step < 86'400) {
        set_string(time_step / 3'600, "hrs");
    } else if (86'400 <= time_step) {
        set_string(time_step / 86'400, "days");
    }
}
