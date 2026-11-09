# Headless unit tests: no window, no GPU (docs/TESTING_AND_CI.md).
find_package(Catch2 3 REQUIRED CONFIG)

add_executable(lpe_unit_tests
    unit/core/result_test.cpp
    unit/core/smoke_test.cpp
    unit/platform/window_desc_test.cpp
)

target_link_libraries(lpe_unit_tests PRIVATE
    engine
    Catch2::Catch2WithMain
)

lpe_set_project_warnings(lpe_unit_tests)

include(Catch)
catch_discover_tests(lpe_unit_tests)
