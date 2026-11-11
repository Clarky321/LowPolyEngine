# GPU tier: needs a desktop session and an OpenGL 4.5 driver. Every test has
# the "gpu" label; the default test presets exclude it, the *-gpu presets run it.
# Tests run from the repository root so island-preview finds assets/.

set(fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# Files written by GPU tests (screenshots); inside the build tree, never in the sources.
set(gpu_output "${CMAKE_BINARY_DIR}/gpu-test-output")

# Engine-level GPU tests (Catch2): application loop, renderer, frame capture.
add_executable(lpe_gpu_tests
    application_gpu_test.cpp
    renderer_gpu_test.cpp
    texture_gpu_test.cpp
)
# glad: the texture tests inspect OpenGL objects directly (handles, row order,
# sampling state). The renderer loads the function pointers.
target_link_libraries(lpe_gpu_tests PRIVATE engine glad Catch2::Catch2WithMain)
lpe_set_project_warnings(lpe_gpu_tests)
catch_discover_tests(lpe_gpu_tests
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    PROPERTIES LABELS "gpu"
)

function(lpe_add_gpu_test name)
    add_test(NAME ${name} COMMAND ${ARGN})
    set_tests_properties(${name} PROPERTIES
        LABELS "gpu"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
endfunction()

# Context, shaders, buffers and frames run without any OpenGL error (exit code 0).
lpe_add_gpu_test(island_preview_render_smoke island-preview --frames 30)

# The --screenshot path used for docs/screenshots works end to end.
lpe_add_gpu_test(island_preview_screenshot
    island-preview --frames 5 --screenshot "${gpu_output}/island-preview.png")
set_tests_properties(island_preview_screenshot PROPERTIES
    PASS_REGULAR_EXPRESSION "saved screenshot '[^']*island-preview\\.png' \\([0-9]+x[0-9]+\\)")

# Shader problems must be reported with the file name and the driver's log.
lpe_add_gpu_test(island_preview_shader_compile_error
    island-preview --frames 1 --assets "${fixtures}/compile-error")
set_tests_properties(island_preview_shader_compile_error PROPERTIES
    PASS_REGULAR_EXPRESSION "failed to compile shader '[^']*mesh\\.vert'")

lpe_add_gpu_test(island_preview_shader_link_error
    island-preview --frames 1 --assets "${fixtures}/link-error")
set_tests_properties(island_preview_shader_link_error PROPERTIES
    PASS_REGULAR_EXPRESSION "failed to link shader program")

lpe_add_gpu_test(island_preview_missing_shader
    island-preview --frames 1 --assets "${fixtures}/does-not-exist")
set_tests_properties(island_preview_missing_shader PROPERTIES
    PASS_REGULAR_EXPRESSION "cannot open file '[^']*mesh\\.vert'")

# A shader that compiles and links but lacks what the renderer needs is named
# with the missing variable, instead of rendering a silently empty frame.
lpe_add_gpu_test(island_preview_shader_missing_uniform
    island-preview --frames 1 --assets "${fixtures}/missing-uniform")
set_tests_properties(island_preview_shader_missing_uniform PROPERTIES
    PASS_REGULAR_EXPRESSION "uniform mat4 'uModelViewProjection' is missing or unused")

# Without the material base color the renderer cannot apply materials (M2.6).
lpe_add_gpu_test(island_preview_shader_missing_base_color
    island-preview --frames 1 --assets "${fixtures}/missing-base-color")
set_tests_properties(island_preview_shader_missing_base_color PROPERTIES
    PASS_REGULAR_EXPRESSION "uniform vec4 'uBaseColor' is missing or unused")

# Without the base color texture sampler textured materials cannot draw (M2.7).
lpe_add_gpu_test(island_preview_shader_missing_base_color_texture
    island-preview --frames 1 --assets "${fixtures}/missing-base-color-texture")
set_tests_properties(island_preview_shader_missing_base_color_texture PROPERTIES
    PASS_REGULAR_EXPRESSION "uniform sampler2D 'uBaseColorTexture' is missing or unused")

# Shaders without lighting (as of M2.7) are named by the first missing
# lighting uniform (M2.8).
lpe_add_gpu_test(island_preview_shader_missing_lighting
    island-preview --frames 1 --assets "${fixtures}/missing-lighting")
set_tests_properties(island_preview_shader_missing_lighting PROPERTIES
    PASS_REGULAR_EXPRESSION "uniform mat3 'uNormalMatrix' is missing or unused")

# An optional input (normals, UVs) that is used at the wrong location would read
# the wrong buffer range; it is named with both locations.
lpe_add_gpu_test(island_preview_shader_wrong_attribute_location
    island-preview --frames 1 --assets "${fixtures}/wrong-attribute-location")
set_tests_properties(island_preview_shader_wrong_attribute_location PROPERTIES
    PASS_REGULAR_EXPRESSION "vertex input 'aNormal' must be at location 2 \\(found at 4\\)")
