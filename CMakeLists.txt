cmake_minimum_required(VERSION 3.25)

project(LowPolyEngine
    VERSION 0.1.0
    DESCRIPTION "Low-poly 3D engine and editor"
    LANGUAGES CXX)

# C++20 without compiler extensions (AGENTS.md section 2).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LPE_BUILD_TESTS "Build the headless unit tests" ON)
option(LPE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in first-party code" OFF)

# All executables of a configuration go to one folder (build/<preset>/bin/<Config>/
# with Visual Studio) so scripts/windows/run.bat can find them by target name.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

add_subdirectory(engine)

if(LPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# After enable_testing(), so the sandbox can register its smoke test.
add_subdirectory(sandbox/island-preview)
