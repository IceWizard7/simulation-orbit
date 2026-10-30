#include <cstddef>
#include <imgui.h>
#include <iostream>
#include <raylib.h>
#include <rlImGui.h>
#include <thread>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "config.hpp"
#include "force_pairs.hpp"
#include "simulation.hpp"
#include "ui.hpp"

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
        if (simulation::celestial_bodies.enabled[i]) {
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
    InitWindow(config::WINDOW_WIDTH, config::WINDOW_HEIGHT, "simulation-orbit");
    SetExitKey(KEY_NULL); // So ESC doesn't close the window, but rather dismisses other UI actions (ImGui)
    SetTargetFPS(config::TARGET_FPS);

    rlImGuiSetup(true);
    // Keep global directional keyboard (ImGuiConfigFlags_NavEnableKeyboard) navigation disabled (causes weird behavior)
    // (ex. ImGui still wants keyboard & mice inputs for no reason)
    // Relevant tab selection (ex. between input fields) still works without this config flag.
    // We do sacrifice some functionality (ex. arrow keys in ImGui::Combo), but that's the better trade for now.

    // Loading begin
    ui_helpers::load_star_background();

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (simulation::celestial_bodies.enabled[i]) simulation::celestial_bodies.planet_visuals[i].load_planet_visual();
    }

    const ImGuiIO& io = ImGui::GetIO();

    config::imgui_ui_font = io.Fonts->AddFontFromFileTTF(
        "resources/JetBrainsMono-Regular.ttf",
        16.0f
    );

    config::imgui_ui_font_small = io.Fonts->AddFontFromFileTTF(
        "resources/JetBrainsMono-Regular.ttf",
        11.0f
    );

    // Loading done

    config::timer.resume();

    simulation::publish_snapshot();
    std::jthread cpu_thread(simulation::simulate_cpu);

    #ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
    #else
    while (!WindowShouldClose()) {
        ui_helpers::UpdateDrawFrame();
    }
    #endif

    cpu_thread.request_stop();
    config::timer.pause();
    cpu_thread.join();

    // Unloading begin

    for (std::size_t i = 0; i < NUM_CELESTIAL_BODIES; i++) {
        if (simulation::celestial_bodies.enabled[i]) simulation::celestial_bodies.planet_visuals[i].unload_planet_visual();
    }

    ui_helpers::unload_star_background();

    // Unloading done

    rlImGuiShutdown();
    CloseWindow();
    std::cout << '\n';

    const bool csv_output_succeeded = simulation::finalize_csv_output();
    simulation::print_final_state();

    return !csv_output_succeeded;
}
