# The engine starts as one static library with internal folders per domain
# (docs/PROJECT_STRUCTURE.md). Split it only when boundaries justify it.
find_package(SDL3 REQUIRED CONFIG)

add_library(engine STATIC
    src/core/Version.cpp
    src/platform/WindowDesc.cpp
    src/platform/sdl3/Sdl3Platform.cpp
    src/platform/sdl3/Sdl3Window.cpp
)

target_include_directories(engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(engine PUBLIC cxx_std_20)

# PRIVATE: SDL is an implementation detail of src/platform/sdl3/ (ADR 0002).
# Public headers never include it; consumers only get it as a link dependency.
target_link_libraries(engine PRIVATE SDL3::SDL3)

# The project() version is the single source of truth for the engine version.
set_source_files_properties(src/core/Version.cpp PROPERTIES COMPILE_DEFINITIONS
    "LPE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};LPE_VERSION_MINOR=${PROJECT_VERSION_MINOR};LPE_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
)

lpe_set_project_warnings(engine)
