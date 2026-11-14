# ADR 0018: Dear ImGui as the development and debug UI

- **Status:** Accepted
- **Date:** 2026-11-14
- **Related:** ADR 0002 (SDL3), ADR 0005 (GLAD), ADR 0013 (settings and their limits), ADR 0004 (settings files), `docs/TECH_STACK.md` §5, `docs/ENGINE_PLAN.md` M3 and M5, `docs/milestones/m3-floating-archipelago.md` M3.12, AGENTS.md §3

## Context

M3.12 needs a panel in island-preview that edits every world generation setting and regenerates the world without a restart. `docs/TECH_STACK.md` §5 chose Dear ImGui for the editor; the plan brings it in now as a development and debug UI in the sandbox, with its SDL3 platform and OpenGL 3 renderer backends, while the editor stays M5. The backends are written against SDL and OpenGL, which the engine keeps inside `engine/src/platform/sdl3/` and `engine/src/render/opengl/` (AGENTS.md §3): no OpenGL type may leave the renderer boundary, no SDL type the platform layer.

## Decision

- **Package.** `imgui/1.92.9b` from Conan Center (MIT), default options (static library, no SDL binding built into it; the docking branch is not used). Checked available on 2026-11-14 with `conan list "imgui/*" -r conancenter`; Conan builds it from source for the project's profiles. Recorded in `conanfile.py` and `docs/references/THIRD_PARTY_NOTICES.md`.
- **Backends.** The package ships the official backends as sources (`res/bindings`). `third_party/imgui_backends/` compiles `imgui_impl_sdl3.cpp` and `imgui_impl_opengl3.cpp` from the package of the build into a static library without project warning flags (like GLAD and stb), with its headers as system includes. The OpenGL 3 backend loads the few OpenGL functions it needs with its own bundled loader and saves and restores the OpenGL state around its draws, so the renderer's state is unchanged afterwards; it does not use GLAD.
- **Where they live.** The engine links `imgui_backends` privately. Only `engine/src/platform/sdl3/Sdl3ImGui.cpp` includes `imgui_impl_sdl3.h`, only `engine/src/render/opengl/GlImGui.cpp` includes `imgui_impl_opengl3.h`; `engine/src/debug/DebugUi.cpp` drives them through the internal functions of `engine/src/debug/ImGuiBackends.hpp`, which use no SDL or OpenGL type. The SDL3 backend gets the native window through `platform::detail::NativeWindowAccess`, a friend of `Window` defined only in the SDL3 layer, so `Window.hpp` still names no SDL type. `Platform::pollEvents` hands every event to the backend while one exists (`forwardEventToDebugUi`).
- **Public API.** `engine/debug/DebugUi.hpp`: `DebugUi::create(window, context)` (one at a time; no `imgui.ini` or log file is written), `beginFrame`, `render`, `endFrame`, `wantsMouse`, `wantsKeyboard`. `ApplicationDesc::debugUi` makes the application loop own one: a UI frame starts after the frame's events and before `Application::update`, where programs build their windows with the Dear ImGui API (`#include <imgui.h>`; the engine links `imgui::imgui` publicly for that), and the UI is drawn after `Application::render`, before the frame is captured or presented, so screenshots include it. `AppContext::debugUi()` returns it (null without one). A program should leave the mouse and keyboard to the UI while it wants them.
- **Renderer diagnostics.** `Renderer::resourceCounts()` returns the meshes, textures and materials alive, from the handle pools, so a test can show that regenerating a world keeps them level (the plan's check for M3.12). It changes nothing in how the renderer draws.

## Consequences

- Dear ImGui is a development tool: the WorldGen panel lives in `sandbox/island-preview/`, and the editor architecture (document model, commands, undo, panels) is still M5's.
- The ImGui context is global state inside Dear ImGui; `DebugUi` allows one at a time and owns its lifetime.
- Two OpenGL loaders exist in the process (GLAD for the renderer, the backend's own for the UI); they load function pointers independently from the same context.
- A Vulkan or Direct3D renderer would need its own ImGui renderer backend behind the same internal functions; the public API stays.
- Text input and the mouse over a UI window are not seen by the camera in island-preview, through `wantsKeyboard` and `wantsMouse`; the engine's input state itself is unchanged.
