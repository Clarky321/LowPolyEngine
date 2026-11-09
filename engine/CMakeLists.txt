# The engine starts as one static library with internal folders per domain
# (docs/PROJECT_STRUCTURE.md). Split it only when boundaries justify it.
find_package(SDL3 REQUIRED CONFIG)
find_package(glm REQUIRED CONFIG)
find_package(stb REQUIRED CONFIG)

add_library(engine STATIC
    src/app/Application.cpp
    src/core/FileSystem.cpp
    src/core/FrameClock.cpp
    src/core/Image.cpp
    src/core/Log.cpp
    src/core/Version.cpp
    src/geometry/MeshData.cpp
    src/platform/Input.cpp
    src/platform/WindowDesc.cpp
    src/platform/sdl3/Sdl3GlContext.cpp
    src/platform/sdl3/Sdl3Input.cpp
    src/platform/sdl3/Sdl3Platform.cpp
    src/platform/sdl3/Sdl3Window.cpp
    src/render/opengl/GlDiagnostics.cpp
    src/render/opengl/GlMesh.cpp
    src/render/opengl/GlRenderer.cpp
    src/render/opengl/GlShaderProgram.cpp
)

target_include_directories(engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(engine PUBLIC cxx_std_20)

# PRIVATE: SDL is used only in src/platform/sdl3/, GLAD only in
# src/render/opengl/ and stb only in src/core/Image.cpp (ADR 0002, 0005, 0006).
# Public headers include none of them; consumers only get them as link dependencies.
target_link_libraries(engine PRIVATE
    SDL3::SDL3
    glad
    stb::stb
)

# PUBLIC: engine/core/Math.hpp aliases GLM types in public headers (ADR 0007).
target_link_libraries(engine PUBLIC
    glm::glm
)

# The project() version is the single source of truth for the engine version.
set_source_files_properties(src/core/Version.cpp PROPERTIES COMPILE_DEFINITIONS
    "LPE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};LPE_VERSION_MINOR=${PROJECT_VERSION_MINOR};LPE_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
)

lpe_set_project_warnings(engine)
