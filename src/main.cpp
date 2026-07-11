#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "config.hpp"
#include "simulation.hpp"
#include "timer.hpp"
#include "ui.hpp"

// TODO: Parallel Execution?
// TODO: Analyze which forces we can skip calculating (or calculate ex. every 1000 steps) to reach certain accuracy
// TODO: Think of Topic & research questions
// TODO: Internal planet rotation

int main(const int argc, char* argv[]) {
    const int err = runtime_config::parse_cli_args(argc, argv);
    if (err != 0) return err;
    if (runtime_config::exit_immediately) return 0;

    if (runtime_config::headless) {
        while (ui::simulated_years() < runtime_config::TARGET_TOTAL_SIMULATION_TIME) {
            simulation::simulate_step();
        }

        config::timer.pause();
        simulation::print_final_state();
        return 0;
    }

    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(config::WINDOW_WIDTH, config::WINDOW_HEIGHT, "Umlaufbahn Simulation");
    SetTargetFPS(config::TARGET_FPS);

    for (auto& celestial_body : simulation::celestial_bodies) {
        celestial_body.planet_visual.load_planet_visual();
    }

    config::uiFont = LoadFontEx(
        "resources/JetBrainsMono-Regular.ttf",
        96,
        nullptr,
        0
    );

    GenTextureMipmaps(&config::uiFont.texture);
    SetTextureFilter(config::uiFont.texture, TEXTURE_FILTER_TRILINEAR);

    config::timer.resume();

    std::jthread cpu_thread(simulation::simulate_cpu);
    simulation::publish_snapshot();

    #ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
    #else
    while (!WindowShouldClose()) {
        ui::UpdateDrawFrame();
    }
    #endif

    cpu_thread.request_stop();
    config::timer.pause();
    cpu_thread.join();

    for (auto& celestial_body : simulation::celestial_bodies) {
        celestial_body.planet_visual.unload_planet_visual();
    }

    UnloadFont(config::uiFont);
    CloseWindow();
    printf("\n");

    simulation::print_final_state();

    return 0;
}
