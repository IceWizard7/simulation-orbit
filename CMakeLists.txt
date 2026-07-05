cmake_minimum_required(VERSION 4.2)
project(simulation-orbit)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

set(RAYLIB_VERSION 6.0)

FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
)

FetchContent_MakeAvailable(raylib)

add_executable(simulation-orbit
        src/main.cpp
        src/utils.cpp
        src/planet_visual.cpp
        src/celestial_body.cpp
        src/celestial_body.cpp
        src/ui.cpp
)

target_include_directories(simulation-orbit PRIVATE include)

target_compile_definitions(raylib PRIVATE
        SUPPORT_FILEFORMAT_JPG=1
)

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
        -ffast-math
        >
)
