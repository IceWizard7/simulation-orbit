cmake_minimum_required(VERSION 4.2)
project(SimulationUmlaufbahn)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

set(RAYLIB_VERSION 6.0)

FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
)

FetchContent_MakeAvailable(raylib)


add_executable(SimulationUmlaufbahn
        src/main.cpp)

target_link_libraries(SimulationUmlaufbahn PRIVATE raylib)

# Required when linking raylib statically on macOS
if (APPLE)
    target_link_libraries(SimulationUmlaufbahn PRIVATE
            "-framework IOKit"
            "-framework Cocoa"
            "-framework OpenGL"
    )
endif()
