# ADR 0001: Initial technology stack

- **Status:** Accepted for the M0 foundation (see *Verification*); the remaining items stay candidates
- **Date:** 2026-11-08 (proposed), 2026-11-10 (accepted)

## Context

The project is a new low-poly 3D engine with a native editor, C++ core and Lua gameplay scripting. It needs a reproducible build, an initial desktop renderer, procedural-world tests and a path to additional graphics APIs.

## Decision

- C++20 for engine/editor code.
- CMake + Conan 2 as the build/dependency foundation.
- SDL3 for window, input, event and controller handling.
- OpenGL 4.5 Core as the first renderer backend, loaded through GLAD 2.
- GLM, spdlog and EnTT as initial supporting candidates.
- Lua 5.5.x for gameplay/content scripting; LuaBridge3 or a project-owned Lua C API facade after compatibility proof.
- Dear ImGui as first editor UI.
- Catch2 + CTest for tests.
- Python and Windows batch only as wrappers/tooling; Docker for pinned Linux builds/headless CI.

## Alternatives considered

- GLFW instead of SDL3: smaller and focused, but SDL3 offers a broader game-input/platform layer.
- SFML: convenient, but less suitable as the base platform layer for a custom 3D renderer.
- SDL_GPU: useful for Vulkan/D3D12/Metal-class backends, but does not provide OpenGL.
- BGFX: attractive for multi-backend requirements, but delegates low-level backend implementation.
- Raven: promising editor GUI, but its public API is under active development and integration is unverified.

## Consequences

- First milestone remains narrow and buildable.
- OpenGL-specific code must be isolated so Vulkan/D3D12 can be added later.
- No claim of Vulkan/DirectX support until those backends actually build and pass tests.
- Dependency versions and licenses must be pinned/recorded before implementation.

## Verification (M0, 2026-11-10)

Accepted because these parts are implemented, built with warnings as errors and tested in CI on Windows (MSVC 19.51, `windows-2025-vs2026`) and Linux (GCC 13.3, `ubuntu-24.04`), Debug and Release:

| Decision | Verified by |
|---|---|
| C++20, no extensions | all targets; MSVC `/permissive- /utf-8`, GCC `-std=c++20` |
| CMake + Conan 2 | `CMakePresets.json`, `conanfile.py`, `profiles/`; clean Conan cache builds (M0.1, M0.3) |
| SDL3 for window/events/GL context | `sdl/3.4.16`, `engine/src/platform/sdl3/`, integration tests with the dummy video driver (ADR 0002) |
| OpenGL 4.5 Core through GLAD 2 | `engine/src/render/opengl/`, test triangle; GLAD vendored instead of Conan (ADR 0005); GPU tests run locally only |
| Catch2 + CTest | unit, integration and `gpu` test tiers |
| Python and batch as tooling only | `scripts/windows/*.bat`, `scripts/python/generate_glad.py`, `requirements-dev.txt` |

Not verified yet, still candidates to be confirmed by the milestone that first needs them: GLM, spdlog, EnTT, Lua 5.5 (Conan Center has 5.5.0, not 5.5.1) with LuaBridge3 or a project-owned facade, Dear ImGui, Docker.

## Revisit when

- A non-OpenGL backend becomes a real requirement.
- SDL3 or Dear ImGui creates a demonstrated integration blocker.
- The chosen Lua binding is incompatible with the pinned Lua package.
