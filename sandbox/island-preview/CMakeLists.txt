# The output name must stay "island-preview": scripts/windows/run.bat runs it by default.
add_executable(island-preview
    main.cpp
)

target_link_libraries(island-preview PRIVATE engine)

lpe_set_project_warnings(island-preview)

if(LPE_BUILD_TESTS)
    # Opens the real window code path with SDL's "dummy" video driver, which
    # needs no display or GPU, so it runs in ordinary CI (docs/TESTING_AND_CI.md).
    add_test(NAME island_preview_window_smoke COMMAND island-preview --frames 3)
    set_tests_properties(island_preview_window_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy"
        LABELS "smoke"
    )
endif()
