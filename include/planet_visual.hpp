#pragma once

#include <optional>
#include <raylib.h>

#include "utils.hpp"

Mesh make_equirectangular_sphere_mesh(float radius, int rings, int slices);

class PlanetVisual {
public:
    Model model{};
    Texture2D texture{};
    std::optional<str> texture_path;
    bool loaded = false;
    bool failed = false; // Decode/upload failed; !loaded && !failed && texture_path = still loading

    // Renderer thread only (needs the GL context): Mesh + Texture from an already decoded image
    // The caller keeps ownership of `image`; an empty image (failed decode) logs and leaves the body untextured
    void upload_planet_visual(const Image& image);

    // Synchronous decode + upload (used where no loading screen runs, ex. the web build)
    void load_planet_visual();

    void unload_planet_visual();
};
