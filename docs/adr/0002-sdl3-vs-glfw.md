# ADR 0002: SDL3 instead of GLFW for window, input and GL context

- **Status:** Accepted
- **Date:** 2026-11-09
- **Related:** ADR 0001, `docs/TECH_STACK.md` §2

## Context

The engine needs one platform layer for the sandbox (M0–M3) and the editor (M5): window creation, an OpenGL 4.5 Core context, keyboard/mouse input, game controllers and an event loop. Dear ImGui must integrate with it. Later backends (Vulkan) must be able to use the same window.

## Problem

Choose a single platform library. Running two in parallel would duplicate input handling, event loops and context code, and would split ImGui integration.

## Options

| | SDL3 | GLFW |
|---|---|---|
| Conan Center | `sdl/3.4.16` | `glfw/3.4` |
| License | Zlib | Zlib |
| OpenGL context | `SDL_GL_CreateContext` | built in |
| Vulkan surface | yes | yes |
| Game controllers | full gamepad API with mapping database | basic joystick/gamepad polling |
| Other platform services | audio, filesystem paths, clipboard, high-DPI, text input/IME | clipboard, high-DPI; no audio |
| Dear ImGui backend | `imgui_impl_sdl3` (official) | `imgui_impl_glfw` (official) |
| API surface | larger | small and focused |

SDL_GPU (part of SDL3) was considered separately and rejected: it has no OpenGL backend, so it conflicts with the OpenGL 4.5 first renderer.

## Decision

Use **SDL3** (`sdl/3.4.16` from Conan Center, the newest version available there; upstream 3.4.20 is not packaged yet) for window, events, input, controllers and the OpenGL context. Do not use SDL_GPU or SDL_Renderer for 3D rendering. Do not add GLFW.

Reasons: controller support and text input matter for an engine that will ship games and an editor; SDL3 also covers audio later without another dependency. The larger API surface is acceptable because only a small part is used and it stays behind the platform layer.

## Consequences

- SDL headers and types are used only in `engine/src/platform/sdl3/` (and the editor's ImGui SDL3 backend glue). Engine-facing window/input interfaces in `engine/include/engine/platform/` do not expose SDL types.
- The OpenGL backend receives a GL function loader (`SDL_GL_GetProcAddress`) from the platform layer; it does not include SDL itself.
- Pin `sdl/3.4.16` in `conanfile.py` when M0.4 adds it; record the license in `docs/references/THIRD_PARTY_NOTICES.md`.
- Revisit if SDL3 blocks a required feature (e.g. viewport embedding for an editor UI) or causes a demonstrated integration problem on a target platform.
