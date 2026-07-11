#include "config.hpp"

#include <iostream>

namespace runtime_config {
    bool exit_immediately = false;
    bool headless = false;
    double TIME_STEP = 900;
    double half_dt_squared = 0.5 * TIME_STEP * TIME_STEP;
    double half_dt = 0.5 * TIME_STEP;
    str TIME_STEP_STRING = "15 mins";

    double TARGET_TOTAL_SIMULATION_TIME = -1;
    double TARGET_SIMULATION_SPEED = -1;
    double TARGET_STEPS_PER_SECOND = TARGET_SIMULATION_SPEED * config::SECONDS_PER_YEAR / TIME_STEP;
}


runtime_config::ParseRes runtime_config::parse_double(const str& argument_name, const int i, const int argc, char* argv[]) {
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
    if (argc >= 2 && (str(argv[1]) == "--help" || str(argv[1]) == "-h")) {
        print_logo();
        printf("    Options:\n");
        printf("    -v, --version          Show version\n");
        printf("    -h, --help             Show help\n");
        printf("        --headless         Run without Raylib window\n");
        printf("        --dt <seconds>     Configure time step\n");
        printf("        --years <years>    Configure total simulation time\n");
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
            auto [val, err] = parse_double("--dt", i, argc, argv);
            if (err != 0) return err;
            TIME_STEP = val;
            i++;
        } else if  (arg == "--years") {
            auto [val, err] = parse_double("--years", i, argc, argv);
            if (err != 0) return err;
            TARGET_TOTAL_SIMULATION_TIME = val;
            i++;
        } else {
            std::cerr << std::format("Error: Unexpected argument {}.\n", arg);
            return 1;
        }
    }

    if (headless && TARGET_TOTAL_SIMULATION_TIME <= 0) {
        std::cerr << std::format("Error: --headless requires positive --years.\n");
        return 1;
    }

    set_time_step_string();
    half_dt_squared = 0.5 * TIME_STEP * TIME_STEP;
    half_dt = 0.5 * TIME_STEP;

    return 0;
}


void runtime_config::set_time_step_string() {
    auto set_string = [](double val, const str& unit) {
        constexpr double epsilon = 1e-6;
        if (std::abs(val - std::round(val)) < epsilon) {
            // effectively ends in .00
            TIME_STEP_STRING = std::format("{} {}", static_cast<int>(val), unit);
        } else {
            TIME_STEP_STRING = std::format("{:.2f} {}", val, unit);
        }
    };

    if (TIME_STEP < 60) {
        set_string(TIME_STEP, "secs");
    } else if (60 <= TIME_STEP && TIME_STEP < 3'600) {
        set_string(TIME_STEP / 60, "mins");
    } else if (3'600 <= TIME_STEP && TIME_STEP < 86'400) {
        set_string(TIME_STEP / 3'600, "hrs");
    } else if (86'400 <= TIME_STEP) {
        set_string(TIME_STEP / 86'400, "days");
    }
}
