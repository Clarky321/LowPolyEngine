# Test tiers (docs/TESTING_AND_CI.md):
#   unit         headless, no SDL video, no GPU
#   integration  headless; SDL with its "dummy" video driver, no display or GPU
#   rendering    label "gpu": real window + OpenGL 4.5; excluded from the default test presets
find_package(Catch2 3 REQUIRED CONFIG)
include(Catch)

add_executable(lpe_unit_tests
    unit/camera/camera_test.cpp
    unit/camera/free_fly_controller_test.cpp
    unit/core/filesystem_test.cpp
    unit/core/frame_clock_test.cpp
    unit/core/image_test.cpp
    unit/core/log_test.cpp
    unit/core/png_decode_test.cpp
    unit/core/result_test.cpp
    unit/core/smoke_test.cpp
    unit/debug/text_mesh_test.cpp
    unit/geometry/face_normal_test.cpp
    unit/geometry/mesh_data_test.cpp
    unit/geometry/obj_import_test.cpp
    unit/geometry/primitives_test.cpp
    unit/platform/input_state_test.cpp
    unit/platform/window_desc_test.cpp
    unit/render/handle_pool_test.cpp
    unit/render/light_test.cpp
    unit/render/material_test.cpp
    unit/sandbox/island_preview_assets_test.cpp
    unit/worldgen/island_layout_test.cpp
    unit/worldgen/island_mesh_test.cpp
    unit/worldgen/island_shape_test.cpp
    unit/worldgen/noise_test.cpp
    unit/worldgen/random_test.cpp
    unit/worldgen/world_gen_settings_test.cpp
)
target_link_libraries(lpe_unit_tests PRIVATE engine Catch2::Catch2WithMain)
# GPU-free internals of the engine (e.g. render/HandlePool.hpp) are tested directly.
target_include_directories(lpe_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
# The committed assets island-preview loads are checked headless too.
target_compile_definitions(lpe_unit_tests PRIVATE LPE_ASSET_DIRECTORY="${PROJECT_SOURCE_DIR}/assets")
lpe_set_project_warnings(lpe_unit_tests)
catch_discover_tests(lpe_unit_tests)

add_executable(lpe_integration_tests
    integration/application_test.cpp
    integration/input_events_test.cpp
    integration/platform_test.cpp
)
# SDL directly: input tests push synthetic events into SDL's queue.
find_package(SDL3 REQUIRED CONFIG)
target_link_libraries(lpe_integration_tests PRIVATE engine SDL3::SDL3 Catch2::Catch2WithMain)
lpe_set_project_warnings(lpe_integration_tests)
catch_discover_tests(lpe_integration_tests
    PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy"
        LABELS "integration"
)

add_subdirectory(rendering)
