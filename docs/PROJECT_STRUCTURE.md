# Proposed repository structure

The structure deliberately groups code by domain. Avoid generic top-level folders such as `object`, `logic`, `interfaces` and `lowpoly`: these names do not establish clear ownership. “Low-poly” is a visual direction spanning the renderer, materials and world generator, not necessarily a separate subsystem.

```text
lowpoly-engine/
├── CLAUDE.md
├── AGENTS.md
├── .claude/
│   ├── skills/               # tracked: shared Claude Code skills
│   │   └── engine-dev/
│   │       └── SKILL.md
│   └── worktrees/            # ignored; worktrees are not used (DEVELOPMENT_WORKFLOW.md)
├── CMakeLists.txt
├── CMakePresets.json
├── conanfile.py
├── conan.lock                # pinned versions and recipe revisions for both profiles (M4.5, ADR 0024)
├── profiles/
│   ├── windows-msvc
│   └── linux-gcc
├── requirements-dev.txt      # pinned developer tools (Conan, clang-format, Ninja)
├── cmake/
│   ├── CompilerWarnings.cmake # warning policy of first-party targets (lpe_set_project_warnings)
│   ├── EmbedFiles.cmake      # files embedded into generated C++ (Raven's shaders, the editor's layouts; M5.2)
│   └── RavenDependency.cmake # Raven's build options and pin; adds third_party/raven/ (M5.1, ADR 0026)
├── engine/
│   ├── CMakeLists.txt
│   ├── include/engine/
│   │   ├── app/              # Application lifecycle and frame loop (M1)
│   │   ├── core/             # Clock, Log (with listeners, ADR 0030, M5.7), Result, files, images, math aliases, persistent UUIDs (ADR 0020), UTF-8 checks
│   │   ├── platform/         # Engine-facing window/input interfaces
│   │   ├── geometry/         # CPU MeshData, primitive builders (M1, ADR 0007), OBJ subset import (M2.9, ADR 0012)
│   │   ├── render/           # RHI-facing types and renderer interfaces
│   │   ├── scene/            # Entity, components, world, prefab, document; EntityId, SceneId and the Scene core (M4.1, ADR 0020); Name, Transform, Camera, DirectionalLight components (M4.2, ADR 0021); StaticMeshComponent and asset reference checks (M4.3); scene documents (M4.4, ADR 0023); ScriptComponent (M4.5); ParentComponent, prefabs and prefab documents (M4.6, ADR 0025)
│   │   ├── assets/           # Asset IDs, handles, registry interfaces; AssetId, AssetType, AssetPath, AssetRegistry, the asset manifest (M4.3, ADR 0022)
│   │   ├── worldgen/         # Generator settings and pure generation API; deterministic randomness, noise, world settings, island placement, shapes, meshes, palettes, local water, clouds, whole archipelagos and settings files (M3.1–M3.12, ADR 0003, ADR 0004, ADR 0013–0017); settingsFields lists the settings for editors
│   │   ├── scripting/        # Script runtime facade; ScriptRuntime (M4.5, ADR 0024), no Lua types
│   │   ├── camera/           # Camera and camera-controller types
│   │   ├── debug/            # On-screen diagnostics (pixel-font text, M1) and the Dear ImGui debug UI (DebugUi, M3.12, ADR 0018)
│   │   ├── ui/               # Ui.hpp: the Raven UI an application turns on (ApplicationDesc::ui), no Raven/SDL/OpenGL type (M5.2, ADR 0027)
│   │   ├── world/            # The generated world on the GPU (WorldScene), the sky dome (Sky), its sunlight and overview camera (WorldView), moved from island-preview for it and the editor (M5.3); a scene's entities for the renderer: SceneDraws (which entities, matrices, light), MeshAssets (mesh assets read once), SceneView (uploaded once, drawn) (M5.6)
│   │   └── project/          # A project folder: ProjectDocument (project.json), Project with createProject/openProject/saveProject, saved as a whole (M5.4, ADR 0028); no editor, UI, window or renderer code
│   └── src/
│       ├── app/
│       ├── core/
│       ├── platform/sdl3/    # also Sdl3RavenHost: Raven's host platform on the SDL3 window (M5.2, ADR 0027)
│       ├── geometry/
│       ├── debug/
│       ├── render/
│       │   ├── rhi/          # planned
│       │   └── opengl/       # also GlRavenBackend: Raven's OpenGL 4.5 backend (M5.2, ADR 0027)
│       ├── scene/
│       ├── assets/
│       ├── worldgen/
│       │   ├── noise/
│       │   ├── island/
│       │   ├── biome/
│       │   ├── water/
│       │   └── clouds/
│       ├── scripting/lua/
│       ├── ui/               # RavenUi: the one owner of Raven's state (Rinit/Rshutdown, frame, checks), RavenHost interface (M5.2, ADR 0027)
│       ├── world/            # WorldScene, Sky, WorldView (M5.3); SceneDraws, MeshAssets, SceneView (M5.6)
│       ├── project/          # the project's open and save, the save journal, ProjectFiles (every file operation, replaced by tests to inject failures) (M5.4)
│       └── camera/
├── editor/                   # lpe-editor on Raven (M5; ADR 0019, ADR 0027); "planned" folders come with later steps
│   ├── CMakeLists.txt        # library `editor` (the editor's code, also linked by tests) and program `lpe-editor`
│   ├── layouts/              # Raven XML, embedded at build time: shell.xml, the shell (M5.2)
│   ├── include/editor/       # UI-free (ADR 0029): document/ (EditorDocument: project, history, dirty state; Command; ComponentValue), commands/ (entity, component and world commands), selection/ (M5.5); panels/ (M5.7: HierarchyRows, the scene's entities as rows of a tree; ConsoleLog, the console's lines and the inbox that receives the log on any thread; M5.8: EulerAngles, the rotation shown as angles; InspectorFields, the groups of values the inspector edits, its asset lists and new components)
│   └── src/
│       ├── main.cpp          # lpe-editor's entry point: --frames, --screenshot, --size (M5.2); a project folder opened at start (M5.6)
│       ├── application/      # EditorShell (the shell application), EditorOptions, ShellLayout (M5.2)
│       ├── document/, commands/, selection/  # the sources of include/editor/ (M5.5)
│       ├── viewport/         # WorldViewport: the generated world drawn from the viewport's draw callback (M5.3), with the document's scene entities, read again when the document's version changes (M5.6); ViewportInput: the editor camera's input (M5.3)
│       ├── panels/           # HierarchyPanel: the scene's entities in Raven's TreeView, create, delete, rename in place, reparent by dragging; ConsolePanel: the log's lines in Raven's ListView with a filter per level (M5.7); InspectorPanel: the selected entity's components, edited through commands (M5.8); the sources of include/editor/panels/
│       ├── panels/           # planned (M5.7–M5.10)
│       └── project/          # planned (M5.11)
├── sandbox/
│   └── island-preview/       # First end-to-end executable/sample: the archipelago (engine/world's WorldScene and sky) with the WorldGen panel (WorldGenPanel, WorldGenPanelUi) or the M1/M2 test scene
├── assets/
│   ├── shaders/
│   ├── materials/
│   ├── palettes/
│   ├── textures/
│   ├── models/
│   ├── worlds/               # World generation settings files (default.json, ADR 0004)
│   └── test-fixtures/
├── tests/
│   ├── unit/
│   │   ├── core/
│   │   ├── worldgen/
│   │   ├── scene/
│   │   ├── assets/
│   │   ├── scripting/
│   │   ├── editor/           # lpe-editor's options (M5.2; the project folder, M5.6); the viewport camera's input with Raven's null backend (M5.3); the document, its commands, history and selection (M5.5); the document's version (M5.6); the hierarchy's rows and panel, the console's lines and panel, the panels with Raven's null backend (M5.7); Euler angles, the inspector's fields and panel (M5.8)
│   │   ├── ui/               # headless Raven: null backend, host platform, injected input (M5.1); the shell layout at several scales (M5.2)
│   │   ├── world/            # the sky and how the world is looked at (M5.3; the sky's tests moved from unit/sandbox/); which scene entities are drawn with which matrices and light, mesh assets read once (M5.6)
│   │   └── project/          # the project document, open and save on disk, failures injected at every operation of a save, the module's dependencies (M5.4); the committed test projects (M5.6)
│   ├── integration/          # headless subsystem tests, the M4 data model end to end (scene_data_model_test.cpp), the Raven host bridge and UI lifetime on SDL's dummy driver (raven_ui_test.cpp, M5.2)
│   ├── rendering/            # Optional GPU-dependent tier
│   └── projects/             # committed test projects, opened by lpe-editor in the GPU tier, in CI and for screenshots: lighthouses/ (M5.6, written by the engine's project code; two parent links added in M5.7, in the engine's format, which a test checks by saving it again)
├── scripts/
│   ├── python/
│   └── windows/
│       ├── _common.bat       # shared helpers (presets, tool lookup)
│       ├── configure.bat
│       ├── build.bat
│       ├── test.bat
│       ├── run.bat
│       ├── format-check.bat
│       └── clean.bat
├── tools/                    # Offline asset/shader utilities, if needed
├── third_party/              # vendored or locally built third-party code; each keeps its licence next to it
│   ├── glad/                 # generated OpenGL loader (ADR 0005)
│   ├── imgui_backends/       # Dear ImGui's SDL3 and OpenGL 3 backends from the Conan package (ADR 0018)
│   ├── stb_image/            # stb's PNG decoder implementation (ADR 0008)
│   ├── stb_image_write/      # stb's PNG encoder implementation (ADR 0006)
│   └── raven/                # Raven 0.2.3, pruned vendored source with its licences; its README.md lists
│                             # the kept and removed paths and the update procedure (M5.1, ADR 0026)
├── docs/
│   ├── ENGINE_PLAN.md
│   ├── TECH_STACK.md
│   ├── PROJECT_STRUCTURE.md
│   ├── TESTING_AND_CI.md
│   ├── DEVELOPMENT_WORKFLOW.md  # branches, one sub-milestone per task, commits, reports
│   ├── milestones/           # step-by-step plan per milestone (mN-<name>.md)
│   ├── experiments/          # technical experiments outside the milestones (raven-ui-evaluation.md)
│   ├── screenshots/          # engine-made captures per milestone (mN/), with README
│   ├── references/
│   └── adr/
├── .github/
│   └── workflows/
│       ├── ci.yml
│       └── release.yml
├── Dockerfile
├── .gitignore
└── README.md
```

## Dependency direction

A useful first-pass dependency direction is:

```text
Editor ───────────────┐
                      v
                 Engine public API
                      |
        ┌─────────────┼──────────────┐
        v             v              v
      Scene         Assets        WorldGen (pure CPU: settings → MeshData/WorldData)
        |             |
        └──────┬──────┘
               v
         Renderer API  ← uploads MeshData produced by WorldGen
               |
          OpenGL backend

WorldGen depends only on Core/math and Geometry (MeshData); it never includes
renderer, SDL or ImGui headers, so it runs in headless tests (ADR 0003, 0007).
Platform (SDL3) supplies window/input/context services.
App (the frame loop) sits on top: it owns Platform, Window, context and Renderer.
Core never depends on App, Platform or Renderer.
Scripting (Lua) uses selected Engine APIs; it does not reach into renderer internals.
```

`app/` is a folder of its own rather than part of `core/` (as first proposed) because the loop depends on the platform and renderer, while `core/` must stay free of them for WorldGen and headless tests.

This is a dependency guide, not a command to create one shared library per box. Early on, `engine` can be one CMake library with internal folders/namespaces. Split targets when build times, ownership or dependency boundaries justify it.

## What not to create initially

Do not create separate `audio`, `animation`, `physics`, `network`, `object`, `logic`, `interfaces` or `devtools` modules simply because those words appear in a long-term roadmap. Add them when the first feature requires them. Keep tests close to the corresponding domain. Editor-only code must not be required by a packaged runtime.
