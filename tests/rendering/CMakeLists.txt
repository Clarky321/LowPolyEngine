# GPU tier: needs a desktop session and an OpenGL 4.5 driver. Every test has
# the "gpu" label; the default test presets exclude it, the *-gpu presets run it.
# Tests run from the repository root so island-preview finds assets/.

set(fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# Files written by GPU tests (screenshots); inside the build tree, never in the sources.
set(gpu_output "${CMAKE_BINARY_DIR}/gpu-test-output")

# Engine-level GPU tests (Catch2): application loop, renderer, frame capture,
# the generated world on the GPU (engine/world, M3.12, M5.3) with the debug UI,
# a scene's entities on the GPU (engine/world's SceneView, M5.6), and the
# editor's hierarchy and console (M5.7).
add_executable(lpe_gpu_tests
    application_gpu_test.cpp
    editor_gpu_test.cpp
    editor_panels_gpu_test.cpp
    editor_scene_gpu_test.cpp
    editor_viewport_gpu_test.cpp
    renderer_gpu_test.cpp
    scene_view_gpu_test.cpp
    texture_gpu_test.cpp
    world_scene_gpu_test.cpp
)
# glad: the texture tests inspect OpenGL objects directly (handles, row order,
# sampling state). The renderer loads the function pointers.
target_link_libraries(lpe_gpu_tests PRIVATE engine glad Catch2::Catch2WithMain)
# The editor on Raven (M5.2, M5.3, M5.6, M5.7): the editor's code, Raven's API, the
# engine's OpenGL state comparison (engine/src/render/opengl/GlRavenBackend.hpp)
# and the native window, which the viewport test resizes.
find_package(SDL3 REQUIRED CONFIG)
target_link_libraries(lpe_gpu_tests PRIVATE editor raven SDL3::SDL3)
target_include_directories(lpe_gpu_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
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

# World settings from a file (M3.11): the default file gives the default
# world without warnings; --save-settings writes the settings in use; a wrong
# type and an unknown field are named with their path; a missing file is
# named; the options belong to the archipelago.
lpe_add_gpu_test(island_preview_settings_file
    island-preview --settings assets/worlds/default.json --frames 5)
# (Its warnings are those of the settings file, the layout and the clouds; a
# warning about the platform, such as no vsync under Xvfb on Linux, is not.)
set_tests_properties(island_preview_settings_file PROPERTIES
    PASS_REGULAR_EXPRESSION "archipelago: seed 1, 12 of 12 islands"
    FAIL_REGULAR_EXPRESSION "\\[error\\]|\\[warning\\] \\[app\\] (world settings|archipelago|clouds)")
lpe_add_gpu_test(island_preview_save_settings
    island-preview --save-settings "${gpu_output}/saved-settings.json" --frames 1)
set_tests_properties(island_preview_save_settings PROPERTIES
    PASS_REGULAR_EXPRESSION "world settings saved to '[^']*saved-settings\\.json'")
lpe_add_gpu_test(island_preview_settings_wrong_type
    island-preview --settings "${fixtures}/worldgen-settings/wrong-type.json" --frames 1)
set_tests_properties(island_preview_settings_wrong_type PROPERTIES
    PASS_REGULAR_EXPRESSION "wrong-type\\.json': islands\\.count: expected an integer, found string")
lpe_add_gpu_test(island_preview_settings_unknown_field
    island-preview --settings "${fixtures}/worldgen-settings/unknown-field.json" --frames 5)
set_tests_properties(island_preview_settings_unknown_field PROPERTIES
    PASS_REGULAR_EXPRESSION "unknown field 'islands\\.colour' ignored"
    FAIL_REGULAR_EXPRESSION "\\[error\\]")
lpe_add_gpu_test(island_preview_settings_missing_file
    island-preview --settings does-not-exist.json --frames 1)
set_tests_properties(island_preview_settings_missing_file PROPERTIES
    PASS_REGULAR_EXPRESSION "cannot open file '[^']*does-not-exist\\.json'")
lpe_add_gpu_test(island_preview_settings_not_for_test_scene
    island-preview --test-scene --settings assets/worlds/default.json)
set_tests_properties(island_preview_settings_not_for_test_scene PROPERTIES
    PASS_REGULAR_EXPRESSION "usage: island-preview")

# The WorldGen panel (M3.12): the archipelago with the panel open runs
# without errors; the panel belongs to the archipelago.
lpe_add_gpu_test(island_preview_panel_smoke island-preview --panel --frames 30)
set_tests_properties(island_preview_panel_smoke PROPERTIES
    FAIL_REGULAR_EXPRESSION "\\[error\\]")
lpe_add_gpu_test(island_preview_panel_not_for_test_scene island-preview --test-scene --panel)
set_tests_properties(island_preview_panel_not_for_test_scene PROPERTIES
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

# lpe-editor (M5.2, ADR 0027): the editor shell on Raven runs its frames and
# exits with 0 (no OpenGL error, no OpenGL state left changed, no Raven error,
# no Raven memory after shutdown; the exit code says which failed), without an
# error, and without a warning of Raven, the UI or the editor (a warning about
# the platform, such as no vsync under Xvfb on Linux, is not the editor's).
set(lpe_editor_problems "\\[error\\]|\\[warning\\] \\[(raven|ui|editor)\\]")
lpe_add_gpu_test(lpe_editor_smoke lpe-editor --frames 60)
set_tests_properties(lpe_editor_smoke PROPERTIES
    FAIL_REGULAR_EXPRESSION "${lpe_editor_problems}")

# The viewport shows the archipelago of the default world settings (M5.3).
lpe_add_gpu_test(lpe_editor_world_log lpe-editor --frames 5)
set_tests_properties(lpe_editor_world_log PROPERTIES
    PASS_REGULAR_EXPRESSION "viewport: archipelago of seed 1, 12 of 12 islands"
    FAIL_REGULAR_EXPRESSION "${lpe_editor_problems}")

# A project folder (M5.6): the committed test project opens, and the viewport
# draws its five lighthouses; a folder that is not a project stops the editor
# with the error, naming the folder.
lpe_add_gpu_test(lpe_editor_project lpe-editor tests/projects/lighthouses --frames 30)
set_tests_properties(lpe_editor_project PROPERTIES
    PASS_REGULAR_EXPRESSION "viewport: scene 'Main', mesh entities drawn: 5 of 5, meshes uploaded: 1"
    FAIL_REGULAR_EXPRESSION "${lpe_editor_problems}")
lpe_add_gpu_test(lpe_editor_not_a_project lpe-editor "${fixtures}/broken-model" --frames 5)
set_tests_properties(lpe_editor_not_a_project PROPERTIES
    PASS_REGULAR_EXPRESSION "cannot open the project in '[^']*broken-model': [^\n]*'project\\.json' is missing")
lpe_add_gpu_test(lpe_editor_not_a_project_exit lpe-editor "${fixtures}/broken-model" --frames 5)
set_tests_properties(lpe_editor_not_a_project_exit PROPERTIES WILL_FAIL TRUE)

# The hierarchy (M5.7) holds the project's seven entities, built once and
# never rebuilt or relabelled without a change; the console received the run's
# log without an error (a platform's warning, such as no vsync under Xvfb, may
# be among its lines).
lpe_add_gpu_test(lpe_editor_panels lpe-editor tests/projects/lighthouses --frames 30)
set_tests_properties(lpe_editor_panels PROPERTIES
    PASS_REGULAR_EXPRESSION "hierarchy: rows: 7, rebuilds: 1, relabels: 0\n[^\n]*console: lines: [0-9]+ \\(errors: 0, "
    FAIL_REGULAR_EXPRESSION "${lpe_editor_problems}")

# --screenshot saves the frame made by the editor itself, at the --size given.
lpe_add_gpu_test(lpe_editor_screenshot
    lpe-editor --frames 10 --size 1024x640 --screenshot "${gpu_output}/lpe-editor.png")
set_tests_properties(lpe_editor_screenshot PROPERTIES
    PASS_REGULAR_EXPRESSION "saved screenshot '[^']*lpe-editor\\.png' \\(1024x640\\)"
    FAIL_REGULAR_EXPRESSION "${lpe_editor_problems}")
