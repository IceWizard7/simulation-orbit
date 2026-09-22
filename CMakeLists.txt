cmake_minimum_required(VERSION 3.25)
project(simulation-orbit)

set(CMAKE_CXX_STANDARD 20)

option(USE_SYSTEM_RAYLIB "Use an installed raylib package" OFF)
set(RAYLIB_VERSION 6.0)

if (USE_SYSTEM_RAYLIB)
    find_package(raylib ${RAYLIB_VERSION} CONFIG REQUIRED)
else()
    include(FetchContent)

    FetchContent_Declare(
            raylib
            URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )

    FetchContent_MakeAvailable(raylib)

    target_compile_definitions(raylib PRIVATE
            SUPPORT_FILEFORMAT_JPG=1
    )
endif()

add_executable(simulation-orbit
        src/main.cpp
        src/utils.cpp
        src/planet_visual.cpp
        src/celestial_bodies.cpp
        src/ui.cpp
        src/simulation.cpp
        src/config.cpp
        src/force_pairs.cpp
)

target_include_directories(simulation-orbit PRIVATE include)

target_link_libraries(simulation-orbit PRIVATE raylib)

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
