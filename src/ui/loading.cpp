#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <raylib.h>
#include <string>
#include <system_error>
#include <vector>

#include "planet_visual.hpp"
#include "simulation.hpp"
#include "ui.hpp"

// Startup resource loading: Textures are decoded (CPU, no GL) on decode threads and uploaded on the
// renderer thread, which owns the GL context, while a progress screen keeps the window responsive

namespace {
    struct PendingTexture {
        std::size_t id;
        std::future<Image> decoded;
    };

    std::vector<PendingTexture> pending_textures; // Renderer thread only

    constexpr int MAX_TEXTURE_WIDTH = 4096; // GPU limit safety and VRAM

    // Decode thread: CPU only (decode + downscale), no GL
    Image decode_texture(const str& path) {
        Image image = LoadImage(path.c_str());

        if (image.data && image.width > MAX_TEXTURE_WIDTH) {
            ImageResize(&image, MAX_TEXTURE_WIDTH, image.height * MAX_TEXTURE_WIDTH / image.width);
        }
        return image;
    }

    struct LoadJob {
        str path;
        std::function<void(const Image&)> upload; // Renderer thread only (needs the GL context)
        std::uintmax_t bytes = 0; // progress weight: decode time ~ file size
        std::future<Image> decoded;
        bool done = false;
    };

    std::vector<LoadJob> make_load_jobs() {
        std::vector<LoadJob> jobs;

        jobs.push_back({
            ui_config::STAR_BACKGROUND_PATH, [](const Image& image) {
                ui_helpers::upload_star_background(image);
            }
        });

        for (auto& body_texture : ui_config::body_textures) {
            PlanetVisual& visual = body_texture;
            if (!visual.texture_path) continue;

            jobs.push_back({
                *visual.texture_path, [&body_texture](const Image& image) {
                    body_texture.upload_planet_visual(image);
                }
            });
        }

        for (LoadJob& job : jobs) {
            std::error_code error;
            job.bytes = std::filesystem::file_size(job.path, error);
            if (error || job.bytes == 0) job.bytes = 1; // missing file: still counts as one finished step
        }

        return jobs;
    }

#ifndef __EMSCRIPTEN__
    void draw_centered_text(const Font& font, const str& text, const float y, const float size, const Color color) {
        const Vector2 extent = MeasureTextEx(font, text.c_str(), size, 1.0f);
        DrawTextEx(font, text.c_str(), {(static_cast<float>(GetScreenWidth()) - extent.x) * 0.5f, y}, size, 1.0f, color);
    }

    void draw_loading_frame(const Font& font, const float progress, const str& last_loaded) {
        // Recomputed every frame: MaximizeWindow() and user resizes change the size during loading
        const auto width = static_cast<float>(GetScreenWidth());
        const auto height = static_cast<float>(GetScreenHeight());
        const float center_x = width * 0.5f;
        const float center_y = height * 0.5f;

        ClearBackground(BLACK); // same as the 3D view

        // "Alive" indicator: keeps moving while a single large texture decodes and the bar can't advance
        constexpr float orbit_radius = 22.0f;
        const Vector2 star{center_x, center_y - 90.0f};
        const auto angle = static_cast<float>(GetTime() * 2.0);
        DrawRing(star, orbit_radius - 0.75f, orbit_radius + 0.75f, 0.0f, 360.0f, 64, {70, 70, 70, 255});
        DrawCircleV(star, 6.0f, {255, 230, 40, 255});
        DrawCircleV({star.x + orbit_radius * std::cos(angle), star.y + orbit_radius * std::sin(angle)}, 3.5f, {40, 120, 204, 255});

        draw_centered_text(font, "simulation-orbit", center_y - 45.0f, 28.0f, RAYWHITE);

        // Progress bar
        const float bar_width = std::min(420.0f, width - 64.0f);
        constexpr float bar_height = 10.0f;
        const Rectangle bar{center_x - bar_width * 0.5f, center_y + 5.0f, bar_width, bar_height};
        DrawRectangleRounded(bar, 1.0f, 8, {40, 40, 40, 255});

        if (progress > 0.0f) {
            const Rectangle fill{bar.x, bar.y, std::max(bar_height, bar.width * std::clamp(progress, 0.0f, 1.0f)), bar.height};
            DrawRectangleRounded(fill, 1.0f, 8, {40, 120, 204, 255});
        }

        const int percent = static_cast<int>(std::lround(std::clamp(progress, 0.0f, 1.0f) * 100.0f));
        draw_centered_text(font, "Loading textures... " + std::to_string(percent) + " %", center_y + 30.0f, 18.0f, {200, 200, 200, 255});
        draw_centered_text(font, last_loaded, center_y + 54.0f, 14.0f, {120, 120, 120, 255});
    }
#endif
}

bool ui_helpers::load_resources_with_progress() {
    std::vector<LoadJob> jobs = make_load_jobs();

#ifdef __EMSCRIPTEN__
    // The browser shell already shows download progress, and a blocking loop can't present frames here
    for (LoadJob& job : jobs) {
        Image image = LoadImage(job.path.c_str());
        job.upload(image);
        UnloadImage(image);
    }
    return true;
#else
    std::uintmax_t total_bytes = 0;
    for (const LoadJob& job : jobs) total_bytes += job.bytes;

    // Load at the DPI-scaled pixel size and draw at the logical size, otherwise text is blurry on Retina displays
    const float dpi_scale = std::max(1.0f, GetWindowScaleDPI().y);
    Font font = LoadFontEx("resources/JetBrainsMono-Regular.ttf", static_cast<int>(std::lround(28.0f * dpi_scale)), nullptr, 0);
    const bool own_font = font.texture.id != 0;
    if (own_font) {
        SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        font = GetFontDefault();
    }

    // Each 8k texture decodes to ~100 MB of RGB; cap parallel decodes to bound peak memory
    std::size_t next_to_start = 0;
    std::size_t in_flight = 0;
    std::size_t finished = 0;
    std::uintmax_t finished_bytes = 0;
    str last_loaded = "Preparing";

    bool completed = true;

    while (finished < jobs.size()) {
        constexpr std::size_t MAX_IN_FLIGHT = 3;

        if (WindowShouldClose()) {
            completed = false;
            break;
        }

        while (in_flight < MAX_IN_FLIGHT && next_to_start < jobs.size()) {
            LoadJob& job = jobs[next_to_start++];
            // launch::async: a deferred task would only run at get(), on the renderer thread
            job.decoded = std::async(std::launch::async, [path = job.path] { return LoadImage(path.c_str()); });
            in_flight++;
        }

        // Upload at most one finished image per frame: An 8k upload + mipmaps takes a noticeable moment
        for (LoadJob& job : jobs) {
            if (job.done || !job.decoded.valid()) continue;
            if (job.decoded.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;

            Image image = job.decoded.get();
            job.upload(image); // failed decode (empty image) logs inside and leaves the body untextured
            UnloadImage(image); // the GPU has its own copy now

            job.done = true;
            in_flight--;
            finished++;
            finished_bytes += job.bytes;
            last_loaded = std::filesystem::path(job.path).filename().string();
            break;
        }

        const float progress = static_cast<float>(
            static_cast<double>(finished_bytes) / static_cast<double>(std::max<std::uintmax_t>(total_bytes, 1))
        );

        BeginDrawing(); // EndDrawing() polls events: the window stays responsive and closable
        draw_loading_frame(font, progress, last_loaded);
        EndDrawing();
    }

    // Closed early: wait for running decodes (get() blocks) and free their images; already uploaded
    // resources are released by the regular unload functions
    for (LoadJob& job : jobs) {
        if (job.decoded.valid()) UnloadImage(job.decoded.get());
    }

    if (own_font) UnloadFont(font);
    return completed;
#endif
}

void ui_helpers::register_catalog_textures() {
    for (std::size_t i = 0; i < NUM_CATALOG_BODIES; i++) {
        const auto& path = simulation::celestial_bodies.catalog_texture_paths[i];
        if (!simulation::celestial_bodies.enabled[i] || !path) continue;

        simulation::celestial_bodies.texture_ids[i] = ui_config::body_textures.size();
        ui_config::body_textures.push_back(PlanetVisual{.texture_path = *path});
    }
}

std::optional<std::size_t> ui_helpers::register_custom_texture_file(const str& path) {
    for (std::size_t id = 0; id < ui_config::body_textures.size(); id++) {
        const auto& visual = ui_config::body_textures[id];
        // Loaded or still loading: reuse; a failed entry is retried as a new one
        if (visual.texture_path == path && !visual.failed) return id;
    }

    // Cheap synchronous check, so a missing file still fails immediately (drop error, import warning)
    if (!FileExists(path.c_str())) return std::nullopt;

    const std::size_t id = ui_config::body_textures.size();
    ui_config::body_textures.push_back(PlanetVisual{.texture_path = path}); // Placeholder: Drawn untextured until uploaded

#ifdef __EMSCRIPTEN__
    // Web: Synchronous, like the startup loader (thread pool is sized for the simulation thread)
    Image image = decode_texture(path);
    ui_config::body_textures[id].upload_planet_visual(image);
    UnloadImage(image);
    ui_config::body_textures[id].failed = !ui_config::body_textures[id].loaded;
#else
    // launch::async: Deferred would decode on the renderer thread at get()
    pending_textures.push_back({id, std::async(std::launch::async, decode_texture, path)});
#endif

    return id;
}

void ui_helpers::poll_texture_uploads() {
    // At most 1 upload per frame: An upload + mipmaps of a large image takes a noticeable moment
    for (auto it = pending_textures.begin(); it != pending_textures.end(); ++it) {
        if (it->decoded.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;

        const Image image = it->decoded.get();
        PlanetVisual& visual = ui_config::body_textures[it->id];
        visual.upload_planet_visual(image); // Empty image: logs and stays unloaded
        visual.failed = !visual.loaded;
        UnloadImage(image);

        pending_textures.erase(it);
        return;
    }
}

void ui_helpers::discard_pending_textures() {
    // get() waits for running decodes; their images were never uploaded
    for (auto& pending : pending_textures) {
        UnloadImage(pending.decoded.get());
    }
    pending_textures.clear();
}
