# Test tiers (docs/TESTING_AND_CI.md):
#   unit         headless, no SDL video, no GPU
#   integration  headless; SDL with its "dummy" video driver, no display or GPU
#   rendering    label "gpu": real window + OpenGL 4.5; excluded from the default test presets
find_package(Catch2 3 REQUIRED CONFIG)
include(Catch)

add_executable(lpe_unit_tests
    unit/core/filesystem_test.cpp
    unit/core/frame_clock_test.cpp
    unit/core/image_test.cpp
    unit/core/log_test.cpp
    unit/core/result_test.cpp
    unit/core/smoke_test.cpp
    unit/platform/window_desc_test.cpp
)
target_link_libraries(lpe_unit_tests PRIVATE engine Catch2::Catch2WithMain)
lpe_set_project_warnings(lpe_unit_tests)
catch_discover_tests(lpe_unit_tests)

add_executable(lpe_integration_tests
    integration/platform_test.cpp
)
target_link_libraries(lpe_integration_tests PRIVATE engine Catch2::Catch2WithMain)
lpe_set_project_warnings(lpe_integration_tests)
catch_discover_tests(lpe_integration_tests
    PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy"
        LABELS "integration"
)

add_subdirectory(rendering)
