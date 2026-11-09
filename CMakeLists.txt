cmake_minimum_required(VERSION 3.25)

# C is only needed for the generated GLAD loader in third_party/glad.
project(LowPolyEngine
    VERSION 0.1.0
    DESCRIPTION "Low-poly 3D engine and editor"
    LANGUAGES C CXX)

# C++20 without compiler extensions (AGENTS.md section 2).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files are UTF-8. Without this MSVC reads them in the system code page
# (e.g. CP1251), which silently corrupts non-ASCII string literals.
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(LPE_BUILD_TESTS "Build the tests" ON)
option(LPE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in first-party code" OFF)

# All executables of a configuration go to one folder (build/<preset>/bin/<Config>/
# with Visual Studio) so scripts/windows/run.bat can find them by target name.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# Before any add_subdirectory(): CMake then (re)writes the CTest files of every
# directory, so tests removed from a folder do not linger in existing build trees.
if(LPE_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(third_party/glad)
add_subdirectory(engine)
add_subdirectory(sandbox/island-preview)

if(LPE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
