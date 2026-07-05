#pragma once

#include <optional>
#include <raylib.h>

Mesh make_equirectangular_sphere_mesh(float radius, int rings, int slices);

class PlanetVisual {
public:
    Model model{};
    Texture2D texture{};
    std::optional<const char*> texture_path;
    bool loaded = false;

    void load_planet_visual();

    void unload_planet_visual();
};
