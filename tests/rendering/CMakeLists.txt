# GPU tier: needs a desktop session and an OpenGL 4.5 driver. Every test has
# the "gpu" label; the default test presets exclude it, the *-gpu presets run it.
# Tests run from the repository root so island-preview finds assets/.

set(fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

function(lpe_add_gpu_test name)
    add_test(NAME ${name} COMMAND ${ARGN})
    set_tests_properties(${name} PROPERTIES
        LABELS "gpu"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
endfunction()

# Context, shaders, buffers and frames run without any OpenGL error (exit code 0).
lpe_add_gpu_test(island_preview_render_smoke island-preview --frames 30)

# Shader problems must be reported with the file name and the driver's log.
lpe_add_gpu_test(island_preview_shader_compile_error
    island-preview --frames 1 --assets "${fixtures}/compile-error")
set_tests_properties(island_preview_shader_compile_error PROPERTIES
    PASS_REGULAR_EXPRESSION "failed to compile shader '[^']*triangle\\.vert'")

lpe_add_gpu_test(island_preview_shader_link_error
    island-preview --frames 1 --assets "${fixtures}/link-error")
set_tests_properties(island_preview_shader_link_error PROPERTIES
    PASS_REGULAR_EXPRESSION "failed to link shader program")

lpe_add_gpu_test(island_preview_missing_shader
    island-preview --frames 1 --assets "${fixtures}/does-not-exist")
set_tests_properties(island_preview_missing_shader PROPERTIES
    PASS_REGULAR_EXPRESSION "cannot open file '[^']*triangle\\.vert'")
