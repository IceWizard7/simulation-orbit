cmake_minimum_required(VERSION 3.25)
project(simulation-orbit)

set(CMAKE_CXX_STANDARD 20)

option(USE_SYSTEM_RAYLIB "Use an installed raylib package" OFF)
set(RAYLIB_VERSION 6.0)

include(FetchContent)

if (USE_SYSTEM_RAYLIB)
    find_package(raylib ${RAYLIB_VERSION} CONFIG REQUIRED)
else()
    FetchContent_Declare(
            raylib
            URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )

    FetchContent_MakeAvailable(raylib)

    if(APPLE)
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_raylib_macos.cmake")
        patch_raylib_macos()
    endif()

    target_compile_definitions(raylib PRIVATE
            SUPPORT_FILEFORMAT_JPG=1
    )
endif()

# Dear ImGui (docking-enabled release)
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.7-docking
        GIT_SHALLOW TRUE
)

# raylib integration for ImGui
FetchContent_Declare(
        rlimgui
        GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
        GIT_TAG Raylib_6_0
        GIT_SHALLOW TRUE
)

FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG 72de27832d1e6299bcc930ce01073d899d420392
)

FetchContent_MakeAvailable(imgui rlimgui implot)

# Compile Dear ImGui as a static library
add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
)

target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

# Compile ImPlot as a static library
add_library(implot STATIC
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        ${implot_SOURCE_DIR}/implot_demo.cpp
)

target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})

target_link_libraries(implot PUBLIC imgui)

# Compile the raylib ImGui backend as a static library
add_library(rlimgui STATIC
        ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)

target_include_directories(rlimgui PUBLIC ${rlimgui_SOURCE_DIR})

target_link_libraries(rlimgui PUBLIC imgui raylib)

add_executable(simulation-orbit
        src/main.cpp
        src/utils.cpp
        src/planet_visual.cpp
        src/celestial_bodies.cpp
        src/simulation.cpp
        src/config.cpp
        src/force_pairs.cpp

        src/ui/2d.cpp
        src/ui/3d.cpp
        src/ui/imgui_helpers.cpp
        src/ui/windows.cpp
        src/ui/generic_helpers.cpp
        src/ui/graphs.cpp
        src/ui/ui.cpp
)

target_link_libraries(simulation-orbit PRIVATE
        raylib
        rlimgui
        implot
)

target_include_directories(simulation-orbit PRIVATE include)

# Required when linking raylib statically on macOS
if (APPLE)
    target_link_libraries(simulation-orbit PRIVATE
            "-framework IOKit"
            "-framework Cocoa"
            "-framework OpenGL"
    )
endif()

# Enable the optimizations only for release-like configurations
target_compile_options(simulation-orbit PRIVATE
        $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:AppleClang,Clang>>:
        -O3
        -mcpu=native
        >
)
