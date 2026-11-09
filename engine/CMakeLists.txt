# The engine starts as one static library with internal folders per domain
# (docs/PROJECT_STRUCTURE.md). Split it only when boundaries justify it.
find_package(SDL3 REQUIRED CONFIG)

add_library(engine STATIC
    src/core/FileSystem.cpp
    src/core/FrameClock.cpp
    src/core/Log.cpp
    src/core/Version.cpp
    src/platform/WindowDesc.cpp
    src/platform/sdl3/Sdl3GlContext.cpp
    src/platform/sdl3/Sdl3Platform.cpp
    src/platform/sdl3/Sdl3Window.cpp
    src/render/opengl/GlDiagnostics.cpp
    src/render/opengl/GlRenderer.cpp
    src/render/opengl/GlShaderProgram.cpp
)

target_include_directories(engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(engine PUBLIC cxx_std_20)

# PRIVATE: SDL is used only in src/platform/sdl3/ and GLAD only in
# src/render/opengl/ (ADR 0002, ADR 0005). Public headers include neither;
# consumers only get them as link dependencies.
target_link_libraries(engine PRIVATE
    SDL3::SDL3
    glad
)

# The project() version is the single source of truth for the engine version.
set_source_files_properties(src/core/Version.cpp PROPERTIES COMPILE_DEFINITIONS
    "LPE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};LPE_VERSION_MINOR=${PROJECT_VERSION_MINOR};LPE_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
)

lpe_set_project_warnings(engine)
