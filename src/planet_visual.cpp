#include "planet_visual.hpp"

Mesh make_equirectangular_sphere_mesh(const float radius, const int rings, const int slices) {
    // builds a custom textured sphere mesh for planet maps
    Mesh mesh{};

    const int columns = slices + 1; // duplicate seam vertices at u=0 and u=1
    mesh.vertexCount = (rings + 1) * columns;
    mesh.triangleCount = rings * slices * 2;

    mesh.vertices = static_cast<float*>(MemAlloc(sizeof(float) * 3 * mesh.vertexCount));
    mesh.normals = static_cast<float*>(MemAlloc(sizeof(float) * 3 * mesh.vertexCount));
    mesh.texcoords = static_cast<float*>(MemAlloc(sizeof(float) * 2 * mesh.vertexCount));
    mesh.indices = static_cast<unsigned short*>(MemAlloc(sizeof(unsigned short) * 3 * mesh.triangleCount));

    for (int row = 0; row <= rings; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(rings);
        const float phi = v * PI;
        const float sin_phi = sinf(phi);
        const float cos_phi = cosf(phi);

        for (int col = 0; col <= slices; ++col) {
            const float u = static_cast<float>(col) / static_cast<float>(slices);
            const float theta = (u - 0.5f) * 2.0f * PI;
            const float nx = sin_phi * cosf(theta);
            const float ny = cos_phi;
            const float nz = -sin_phi * sinf(theta);
            const int vertex = row * columns + col;

            mesh.vertices[vertex * 3] = radius * nx;
            mesh.vertices[vertex * 3 + 1] = radius * ny;
            mesh.vertices[vertex * 3 + 2] = radius * nz;

            mesh.normals[vertex * 3] = nx;
            mesh.normals[vertex * 3 + 1] = ny;
            mesh.normals[vertex * 3 + 2] = nz;

            mesh.texcoords[vertex * 2] = u;
            mesh.texcoords[vertex * 2 + 1] = v;
        }
    }

    int index = 0;
    for (int row = 0; row < rings; ++row) {
        for (int col = 0; col < slices; ++col) {
            const auto top_left = static_cast<unsigned short>(row * columns + col);
            const auto top_right = static_cast<unsigned short>(top_left + 1);
            const auto bottom_left = static_cast<unsigned short>((row + 1) * columns + col);
            const auto bottom_right = static_cast<unsigned short>(bottom_left + 1);

            mesh.indices[index++] = top_left;
            mesh.indices[index++] = bottom_left;
            mesh.indices[index++] = top_right;

            mesh.indices[index++] = top_right;
            mesh.indices[index++] = bottom_left;
            mesh.indices[index++] = bottom_right;
        }
    }

    UploadMesh(&mesh, false);
    return mesh;
}

void PlanetVisual::load_planet_visual() {
    if (loaded || !texture_path.has_value()) return;

    model = LoadModelFromMesh(make_equirectangular_sphere_mesh(1.0f, 64, 128));
    texture = LoadTexture(*texture_path);

    if (texture.id == 0 || model.materials == nullptr) {
        TraceLog(LOG_ERROR, "Could not load planet texture: %s", *texture_path);
        UnloadModel(model);
        model = {};
        texture = {};
        return;
    }

    GenTextureMipmaps(&texture); // generate textures for far-away rendering
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR); // smooth sampling between pixels
    SetTextureWrap(texture, TEXTURE_WRAP_CLAMP); // clamp pixels [0, 1] instead of repeating the opposite side
    SetMaterialTexture(&model.materials[0], MATERIAL_MAP_ALBEDO, texture);

    loaded = true;
}

void PlanetVisual::unload_planet_visual() {
    if (!loaded) return;

    UnloadTexture(texture);
    UnloadModel(model);

    texture = {};
    model = {};
    loaded = false;
}
