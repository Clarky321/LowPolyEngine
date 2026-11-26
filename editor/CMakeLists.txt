# The editor (M5, docs/milestones/m5-minimal-editor.md): a program of its own
# on the engine, with Raven as its only UI (ADR 0019, ADR 0027). A packaged
# game never needs this folder; the engine never includes editor headers.
#
#   editor       the editor's code as a library, so tests link the same code
#                as the program (M5.2: the shell application and its options;
#                M5.3: the world in the viewport and the camera's input;
#                M5.5: the document, the selection and the undoable commands,
#                UI-free, in include/editor/; M5.7: the hierarchy and console
#                panels, their UI-free models in include/editor/panels/; M5.8:
#                the inspector, its Euler angles and fields UI-free)
#   lpe-editor   the program (src/main.cpp)

# The shell's layout, embedded: the editor reads no layout file at run time.
set(editor_layouts "${CMAKE_CURRENT_BINARY_DIR}/generated/EditorLayouts.cpp")
lpe_embed_files("${editor_layouts}" editor::layouts
    shell "${CMAKE_CURRENT_SOURCE_DIR}/layouts/shell.xml"
)

add_library(editor STATIC
    src/application/EditorOptions.cpp
    src/application/EditorShell.cpp
    src/commands/ComponentCommands.cpp
    src/commands/EntityCommands.cpp
    src/commands/WorldCommands.cpp
    src/document/ComponentValue.cpp
    src/document/EditorDocument.cpp
    src/panels/ConsoleLog.cpp
    src/panels/ConsolePanel.cpp
    src/panels/EulerAngles.cpp
    src/panels/HierarchyPanel.cpp
    src/panels/HierarchyRows.cpp
    src/panels/InspectorFields.cpp
    src/panels/InspectorPanel.cpp
    src/selection/Selection.cpp
    src/viewport/ViewportInput.cpp
    src/viewport/WorldViewport.cpp
    ${editor_layouts}
)
# include/editor/: the document, commands and selection (UI-free, ADR 0029)
# and the panels' models; src/: the application, the viewport, the panels and
# other Raven code.
target_include_directories(editor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
# Raven's C API is used directly by the editor's interface code (ADR 0027),
# and the editor's own headers name Raven's types (widget IDs, events): PUBLIC.
# The engine's headers name no Raven type.
target_link_libraries(editor PUBLIC engine raven)
lpe_set_project_warnings(editor)

add_executable(lpe-editor
    src/main.cpp
)
target_link_libraries(lpe-editor PRIVATE editor)
lpe_set_project_warnings(lpe-editor)
