#include <cstddef>
#include <cstdio>
#include <thread>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <raylib.h>

#include "config.hpp"
#include "force_pairs.hpp"
#include "simulation.hpp"
#include "ui.hpp"

// TODO: Parallel Execution?
// TODO: Internal planet rotation

int main(const int argc, char* argv[]) {
    const int err = runtime_config::parse_cli_args(argc, argv, simulation::celestial_bodies);
    if (err != 0) return err;
    if (runtime_config::exit_immediately) return 0;

    if (runtime_config::body_set == runtime_config::BodySet::planet_systems) {
        simulation::apply_planet_systems_approximation();
    }

    force_pairs::initialize_all_pairs(simulation::celestial_bodies);
    force_pairs::set_interaction_set(simulation::planetary_systems);

    if (!simulation::initialize_csv_output()) return 1;
    simulation::initialize_orbit_sampling();

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; ++i) {
        if (simulation::celestial_bodies[i].enabled) {
            config::center_celestial_body_index = i;
            break;
        }
    }

    if (runtime_config::headless) {
        config::timer.resume();

        while (config::steps_simulated.load() < *runtime_config::target_steps) {
            simulation::simulate_step();
        }

        config::timer.pause();
        const bool csv_output_succeeded = simulation::finalize_csv_output();
        simulation::print_final_state();
        return csv_output_succeeded ? 0 : 1;
    }

    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(config::WINDOW_WIDTH, config::WINDOW_HEIGHT, "Umlaufbahn Simulation");
    SetTargetFPS(config::TARGET_FPS);

    // Loading begin
    ui::load_star_background();

    for (auto& celestial_body : simulation::celestial_bodies) {
        if (celestial_body.enabled) celestial_body.planet_visual.load_planet_visual();
    }

    config::uiFont = LoadFontEx(
        "resources/JetBrainsMono-Regular.ttf",
        96,
        nullptr,
        0
    );

    GenTextureMipmaps(&config::uiFont.texture);
    SetTextureFilter(config::uiFont.texture, TEXTURE_FILTER_TRILINEAR);

    // Loading done

    config::timer.resume();

    simulation::publish_snapshot();
    std::jthread cpu_thread(simulation::simulate_cpu);

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

    // Unloading begin

    for (auto& celestial_body : simulation::celestial_bodies) {
        if (celestial_body.enabled) celestial_body.planet_visual.unload_planet_visual();
    }

    ui::unload_star_background();
    UnloadFont(config::uiFont);

    // Unloading end

    CloseWindow();
    printf("\n");

    const bool csv_output_succeeded = simulation::finalize_csv_output();
    simulation::print_final_state();

    return !csv_output_succeeded;
}
