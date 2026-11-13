# The output name must stay "island-preview": scripts/windows/run.bat runs it by default.
# Its GPU smoke tests live in tests/rendering/.
add_executable(island-preview
    main.cpp
    DebugOverlay.cpp
    Sky.cpp
    TestScene.cpp
)

target_link_libraries(island-preview PRIVATE engine)

lpe_set_project_warnings(island-preview)
