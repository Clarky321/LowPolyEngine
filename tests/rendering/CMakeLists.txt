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

# Context, shaders, buffers and frames run without any OpenGL error (exit code 0),
# and the generated archipelago (the default scene, M3.8) logs no error at all.
lpe_add_gpu_test(island_preview_render_smoke island-preview --frames 30)
set_tests_properties(island_preview_render_smoke PROPERTIES
    FAIL_REGULAR_EXPRESSION "\\[error\\]")

# The archipelago of the default seed: all islands placed, counts of
# triangles, water bodies (M3.9) and cloud clusters (M3.10) and the generation
# time in the log.
lpe_add_gpu_test(island_preview_archipelago_log island-preview --frames 5)
set_tests_properties(island_preview_archipelago_log PROPERTIES
    PASS_REGULAR_EXPRESSION "archipelago: seed 1, 12 of 12 islands, [0-9]+ triangles, [0-9]+ lakes, [0-9]+ streams, [0-9]+ waterfalls, 20 clouds, generated in [0-9.]+ ms")

# Another world from the command line, also without errors.
lpe_add_gpu_test(island_preview_seed_smoke island-preview --seed 42 --frames 30)
set_tests_properties(island_preview_seed_smoke PROPERTIES
    FAIL_REGULAR_EXPRESSION "\\[error\\]")

# The M1/M2 test scene stays available behind a flag, and every one of its
# assets loads.
lpe_add_gpu_test(island_preview_test_scene_smoke island-preview --test-scene --frames 30)
set_tests_properties(island_preview_test_scene_smoke PROPERTIES
    FAIL_REGULAR_EXPRESSION "\\[error\\]")

# Options of one scene are rejected for the other.
lpe_add_gpu_test(island_preview_model_needs_test_scene island-preview --model unused.obj)
set_tests_properties(island_preview_model_needs_test_scene PROPERTIES
    PASS_REGULAR_EXPRESSION "usage: island-preview")
lpe_add_gpu_test(island_preview_seed_not_for_test_scene island-preview --test-scene --seed 3)
set_tests_properties(island_preview_seed_not_for_test_scene PROPERTIES
    PASS_REGULAR_EXPRESSION "usage: island-preview")
lpe_add_gpu_test(island_preview_island_not_for_test_scene island-preview --test-scene --island 0)
set_tests_properties(island_preview_island_not_for_test_scene PROPERTIES
    PASS_REGULAR_EXPRESSION "usage: island-preview")

# A close view of one island with its water drawn translucent after the
# opaque geometry (M3.9), without errors; an island the world does not have is
# named.
lpe_add_gpu_test(island_preview_island_smoke island-preview --island 0 --frames 30)
set_tests_properties(island_preview_island_smoke PROPERTIES
    FAIL_REGULAR_EXPRESSION "\\[error\\]")
lpe_add_gpu_test(island_preview_island_out_of_range island-preview --island 12 --frames 1)
set_tests_properties(island_preview_island_out_of_range PROPERTIES
    PASS_REGULAR_EXPRESSION "--island 12: the archipelago has 12 islands")

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

# A broken or missing model file is logged with the file and the reason, and
# the rest of the test scene still runs (M2.10): the second test passes on exit code 0.
lpe_add_gpu_test(island_preview_broken_model
    island-preview --test-scene --frames 5 --model "${fixtures}/broken-model/broken.obj")
set_tests_properties(island_preview_broken_model PROPERTIES
    PASS_REGULAR_EXPRESSION "cannot read OBJ '[^']*broken\\.obj': line 6: position index 4 is out of range")
lpe_add_gpu_test(island_preview_broken_model_runs
    island-preview --test-scene --frames 5 --model "${fixtures}/broken-model/broken.obj")
lpe_add_gpu_test(island_preview_missing_model
    island-preview --test-scene --frames 5 --model "${fixtures}/broken-model/does-not-exist.obj")
set_tests_properties(island_preview_missing_model PROPERTIES
    PASS_REGULAR_EXPRESSION "cannot open file '[^']*does-not-exist\\.obj'")
