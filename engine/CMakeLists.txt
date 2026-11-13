# The engine starts as one static library with internal folders per domain
# (docs/PROJECT_STRUCTURE.md). Split it only when boundaries justify it.
find_package(SDL3 REQUIRED CONFIG)
find_package(glm REQUIRED CONFIG)

# World generation (src/worldgen/) must give the same float results with every
# compiler (ADR 0003): GCC and Clang may otherwise contract a * b + c into a
# fused multiply-add on CPUs that have one. MSVC's default /fp:precise does not
# contract. Every worldgen source belongs in this list.
set(worldgen_sources
    src/worldgen/Archipelago.cpp
    src/worldgen/IslandLayout.cpp
    src/worldgen/IslandMesh.cpp
    src/worldgen/IslandPalette.cpp
    src/worldgen/IslandShape.cpp
    src/worldgen/Noise.cpp
    src/worldgen/Random.cpp
    src/worldgen/WorldGenSettings.cpp
)
set_source_files_properties(${worldgen_sources} PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>"
)

add_library(engine STATIC
    src/app/Application.cpp
    src/camera/Camera.cpp
    src/camera/FreeFlyController.cpp
    src/core/FileSystem.cpp
    src/core/FrameClock.cpp
    src/core/Image.cpp
    src/core/Log.cpp
    src/core/Version.cpp
    src/debug/TextMesh.cpp
    src/geometry/FaceNormal.cpp
    src/geometry/MeshData.cpp
    src/geometry/ObjImport.cpp
    src/geometry/Primitives.cpp
    src/platform/Input.cpp
    src/platform/WindowDesc.cpp
    src/platform/sdl3/Sdl3GlContext.cpp
    src/platform/sdl3/Sdl3Input.cpp
    src/platform/sdl3/Sdl3Platform.cpp
    src/platform/sdl3/Sdl3Window.cpp
    src/render/Lighting.cpp
    src/render/Material.cpp
    src/render/opengl/GlDiagnostics.cpp
    src/render/opengl/GlMesh.cpp
    src/render/opengl/GlRenderer.cpp
    src/render/opengl/GlShaderProgram.cpp
    src/render/opengl/GlTexture.cpp
    ${worldgen_sources}
)

target_include_directories(engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(engine PUBLIC cxx_std_20)

# PRIVATE: SDL is used only in src/platform/sdl3/, GLAD only in
# src/render/opengl/, stb_image and stb_image_write only in src/core/Image.cpp
# (ADR 0002, 0005, 0006, 0008). Public headers include none of them; consumers
# only get them as link dependencies.
target_link_libraries(engine PRIVATE
    SDL3::SDL3
    glad
    stb_image
    stb_image_write
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
