cmake_minimum_required(VERSION 3.24)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture")
endif()

project(WaterLab VERSION 1.0.0 LANGUAGES C CXX)

option(WATERLAB_BUILD_APP "Build the raylib GUI application (fetches raylib)" ON)
option(WATERLAB_ENABLE_SANITIZERS "Enable ASan/UBSan for Debug builds" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# C++17が必須基準。-DCMAKE_CXX_STANDARD=20でも検証可能。
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "WaterLab requires C++17 or later")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ./build/waterlab のように build直下へ実行ファイルを置く。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_library(waterlab_warnings INTERFACE)
target_compile_options(waterlab_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)

add_library(waterlab_sanitizers INTERFACE)
if(WATERLAB_ENABLE_SANITIZERS)
    target_compile_options(waterlab_sanitizers INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    target_link_options(waterlab_sanitizers INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

# ---- core: simulation + physics（raylib非依存） ----
add_library(waterlab_core STATIC
    src/simulation/WaveField.cpp
    src/simulation/BoundaryConditions.cpp
    src/simulation/Disturbance.cpp
    src/simulation/WaveSolverCPU.cpp
    src/simulation/SimulationWorld.cpp
)
target_include_directories(waterlab_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(waterlab_core PUBLIC waterlab_sanitizers PRIVATE waterlab_warnings)

# ---- tests ----
enable_testing()
add_executable(waterlab_tests
    tests/TestMain.cpp
    tests/WaveSolverTests.cpp
    tests/BoundaryTests.cpp
    tests/SimulationWorldTests.cpp
)
target_link_libraries(waterlab_tests PRIVATE waterlab_core waterlab_warnings)
add_test(NAME waterlab_tests COMMAND waterlab_tests)

# ---- headless benchmark ----
add_executable(waterlab_bench tools/Benchmark.cpp)
target_link_libraries(waterlab_bench PRIVATE waterlab_core waterlab_warnings)

# ---- GUI ----
if(WATERLAB_BUILD_APP)
    include(cmake/Dependencies.cmake)

    add_library(waterlab_render STATIC
        src/rendering/HeightMapView.cpp
    )
    target_include_directories(waterlab_render PUBLIC ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(waterlab_render PUBLIC waterlab_core raylib PRIVATE waterlab_warnings)

    add_executable(waterlab
        src/app/Main.cpp
        src/app/ExperimentUI.cpp
    )
    target_link_libraries(waterlab PRIVATE waterlab_render waterlab_warnings)
endif()
