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
├── conan.lock
├── profiles/
│   ├── windows-msvc
│   └── linux-gcc
├── requirements-dev.txt      # pinned developer tools (Conan, clang-format, Ninja)
├── cmake/
│   ├── CompilerWarnings.cmake
│   ├── ProjectOptions.cmake
│   └── Dependencies.cmake
├── engine/
│   ├── CMakeLists.txt
│   ├── include/engine/
│   │   ├── app/              # Application lifecycle and frame loop (M1)
│   │   ├── core/             # Clock, Log, Result, files, images, math aliases
│   │   ├── platform/         # Engine-facing window/input interfaces
│   │   ├── geometry/         # CPU MeshData, primitive builders (M1, ADR 0007), OBJ subset import (M2.9, ADR 0012)
│   │   ├── render/           # RHI-facing types and renderer interfaces
│   │   ├── scene/            # Entity, components, world, prefab, document
│   │   ├── assets/           # Asset IDs, handles, registry interfaces
│   │   ├── worldgen/         # Generator settings and pure generation API; deterministic randomness and noise (M3.1, M3.2, ADR 0003)
│   │   ├── scripting/        # Script runtime facade
│   │   ├── camera/           # Camera and camera-controller types
│   │   └── debug/            # On-screen diagnostics (pixel-font text, M1)
│   └── src/
│       ├── app/
│       ├── core/
│       ├── platform/sdl3/
│       ├── geometry/
│       ├── debug/
│       ├── render/
│       │   ├── rhi/
│       │   └── opengl/
│       ├── scene/
│       ├── assets/
│       ├── worldgen/
│       │   ├── noise/
│       │   ├── island/
│       │   ├── biome/
│       │   ├── water/
│       │   └── clouds/
│       ├── scripting/lua/
│       └── camera/
├── editor/
│   ├── CMakeLists.txt
│   ├── include/editor/
│   │   ├── application/
│   │   ├── document/
│   │   ├── commands/         # Editing commands and undo/redo
│   │   ├── selection/
│   │   └── panels/
│   └── src/
│       ├── application/
│       ├── ui/imgui/
│       ├── viewport/
│       ├── panels/
│       ├── gizmos/
│       └── project/
├── sandbox/
│   └── island-preview/       # First end-to-end executable/sample
├── assets/
│   ├── shaders/
│   ├── materials/
│   ├── palettes/
│   ├── textures/
│   ├── models/
│   ├── worlds/
│   └── test-fixtures/
├── tests/
│   ├── unit/
│   │   ├── core/
│   │   ├── worldgen/
│   │   ├── scene/
│   │   └── scripting/
│   ├── integration/
│   └── rendering/            # Optional GPU-dependent tier
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
├── third_party/
│   └── licenses/
├── docs/
│   ├── ENGINE_PLAN.md
│   ├── TECH_STACK.md
│   ├── PROJECT_STRUCTURE.md
│   ├── TESTING_AND_CI.md
│   ├── DEVELOPMENT_WORKFLOW.md  # branches, one sub-milestone per task, commits, reports
│   ├── milestones/           # step-by-step plan per milestone (mN-<name>.md)
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
