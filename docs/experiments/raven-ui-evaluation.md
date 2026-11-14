# Raven UI evaluation

- **Status:** planned, not started. Nothing has been built or run yet. The facts under *Static inspection* come from reading the sources at the pinned commit on 2026-11-14; the sections under *Report* are filled in by the evaluation itself.
- **Purpose:** decide whether Raven can be the UI framework of the M5 editor instead of Dear ImGui (`docs/TECH_STACK.md` §5, `docs/ENGINE_PLAN.md` §9). Dear ImGui stays the working debug UI (ADR 0018) whatever the result, until a separate, accepted migration.
- **Not part of M4:** no M4 step depends on it, it is not an M4 exit criterion, and it changes no Scene, ECS, Assets or Scripting code (`docs/milestones/m4-scene-data-model.md`).

## Subject

| | |
|---|---|
| Repository | <https://github.com/oscar-soirey/Raven> |
| Pinned commit | `7ab270df3b618de3d11af6033912374b6cd621b7` (2026-11-14, "docs: add Trendshift daily and weekly badges") |
| Version | 0.2.3 (`project(raven VERSION 0.2.3 …)` in its `CMakeLists.txt`) |
| Language | C11 (`C_STANDARD 11`); Objective-C only on Apple platforms |
| License | Apache License 2.0, with a `NOTICE` file; bundled code and fonts: stb_truetype 1.26 (modified, public domain or MIT), stb_image 2.30 (public domain or MIT, optional), nanosvg (zlib, optional), Roboto (Apache 2.0), Roboto Mono (OFL 1.1) — from its `THIRD_PARTY.md` |
| API stability | its README: "still in active development and public API is subject to change" |

The evaluation uses exactly this commit, never a moving reference to `main`.

## Static inspection at the pinned commit (2026-11-14)

Read: `README.md`, `docs/ARCHITECTURE.md`, `CMakeLists.txt`, `cmake/`, `include/raven/raven_core.h`, `raven_host.h`, `raven_backend.h`, `raven_viewport.h`, `src/platform/host/host_platform.c`, `src/render/` (folder layout, shader interfaces), `examples/editor`, `examples/workspace`, `examples/viewport_opengl`, `THIRD_PARTY.md`. Still to read during the evaluation: `docs/XML.md`, `docs/THEMES.md`, `include/Raven.h`, `examples/bevy_editor`, `examples/components`.

**Rendering backends.** Raven has a Vulkan backend (default), a WebGL 2 backend (Emscripten builds only) and a null backend; **there is no desktop OpenGL backend**. `Rbackend_register` + `Rinit(R_BACKEND_CUSTOM)` accept an application backend: `RBackendVTable` with `Init`, `Shutdown`, `CreateSurface`, `DestroySurface`, `CreateTexture`, `UpdateTexture`, `SetTextureFilter`, `DestroyTexture`, `RenderFrame`, `WaitIdle` and the optional `CreateShader`/`DestroyShader`. The UI is a flat array of 80-byte `RDrawInstance`s (rounded rectangles, glyphs, shadows, lines, triangles, glass, blur, curves, shader quads) shaded analytically by signed distance, plus a short command list (`RDrawData`: instances, viewport callbacks, backdrop captures, erase, custom shaders). Consequences for this engine:

- An OpenGL 4.5 backend must be written. The reference shaders `src/render/vulkan/shaders/ui.vert`/`ui.frag` are Vulkan GLSL 450 (push constants, descriptor sets, `gl_VertexIndex`); the WebGL 2 backend (`src/render/webgl/`, about 600 lines of C plus GLSL ES shaders) is the closest model. Code copied from it would bring Apache 2.0 notice obligations into the project; writing it anew from the documented interface avoids that.
- `CreateShader` takes SPIR-V. OpenGL 4.5 Core cannot load SPIR-V without `GL_ARB_gl_spirv`, so custom shader effects stay unsupported (the table allows `NULL`: they are drawn as plain rounded rectangles). Glass and blur need backdrop copies; a backend may skip them.
- `RenderFrame` "renders and presents one frame". With the host platform the engine presents (`SDL_GL_SwapWindow`), so the backend draws into the engine's current framebuffer and must not present; to be confirmed.

**Threads.** The UI thread is the one that calls `Rinit`. By default every backend call runs on a dedicated render thread; `RInitDesc::disable_render_thread` runs them inline on the UI thread, which is required here because the engine's OpenGL context is current on the main thread only. A job system with one worker per spare core paints large widget trees and decodes textures (`RInitDesc::worker_threads`, negative for none). Viewport draw callbacks run on the render thread, i.e. on the UI thread when the render thread is disabled.

**Loop.** Raven has no loop of its own: the application calls `Rdraw_frame()` once per frame (events, posted tasks and timers, layout, paint, submit). With the render thread disabled the submit, and therefore the backend's OpenGL calls, happen inside that call, so it must run while the engine's frame is open (between `Renderer::beginFrame` and `endFrame`).

**Host platform.** `Rhost_platform_register` before `Rinit` makes Raven open no OS window and read no OS event queue. The application gives sizes and scale (`Rhost_window_set_size(window, width, height, scale)`) and injects input: `Rhost_inject_mouse_move`, `_mouse_button`, `_mouse_wheel`, `_mouse_leave`, `_key` (with repeat), `_text` (one code point), `_focus`, `_close`; Raven asks for the cursor, the clipboard and wake-ups through callbacks (`wake` is thread-safe). Every host function must be called from the UI thread. Host windows have no native child windows, so a viewport can only use the texture or the draw-callback mode. The host platform is compiled on every platform (`src/platform/host/host_platform.c`); whether a build with `RAVEN_PLATFORM=null` is enough with it is to be checked.

**Viewport.** Three modes (`raven_viewport.h`): a texture (`Rviewport_set_texture`, `Rviewport_set_flip_y` for OpenGL's bottom-up rows; for OpenGL the documented paths are a Vulkan-shared texture with `GL_EXT_memory_object` or a CPU readback with `Rtexture_update`), a draw callback (`Rviewport_set_draw_callback`; viewport and scissor already set to the widget, UI can be drawn on top), and a native child window (not available with the host platform). Also `Rviewport_set_resize_callback` (UI thread), `Rviewport_set_realtime`, `Rviewport_get_pixel_size`; input reaches a focused viewport through `Rwidget_set_event_callback`. The engine's renderer has no render-target API (M2), so the texture mode can only be tested here with a CPU readback of a rendered frame; a render-to-texture path would be an engine change outside this experiment.

**Layout and widgets.** XML documents (`Window`, `MenuBar`, `Splitter`, `TreeView`, `TextInput`, `TabView`, `ListView`, `Viewport`, … in `examples/editor/editor.xml`; `DockArea`, `HeaderBar`, `FileBrowser`, `TextEditor` in `examples/workspace/workspace.xml`). Docking: `DockArea` with tabs, drag to dock, tearing off into windows, saved and restored layouts (`Rdock_area_save_layout`/`Rdock_area_load_layout`). Themes from files; memory through its own TLSF allocator with statistics (`Rmemory_get_stats`; `allocation_count` 0 after `Rshutdown` means no leak).

**CMake, as read.**

| Option | Default | Effect | Prototype |
|---|---|---|---|
| `RAVEN_BACKEND_VULKAN` | ON | compiles the Vulkan backend; only then is `cmake/RavenVulkanHeaders.cmake` included | OFF |
| `RAVEN_FETCH_VULKAN_HEADERS` | ON | downloads Khronos Vulkan-Headers with `FetchContent` when no SDK is found — the only download in Raven's CMake files, reachable only with the Vulkan backend | OFF |
| `RAVEN_BUILD_EXAMPLES`, `RAVEN_BUILD_TESTS`, `RAVEN_BUILD_TOOLS` | ON when Raven is the top-level project | examples, tests, the designer | OFF |
| `RAVEN_BUILD_WEB` | ON | compiles `raven_web` pages that drive an installed Chromium-based browser | OFF (not in the original list; found here) |
| `RAVEN_WAYLAND` | ON (Linux, BSD) | Wayland beside X11, libraries opened at run time | OFF for the first Linux attempt |
| `RAVEN_BUILD_SHARED` | OFF | shared library | OFF |
| `RAVEN_PLATFORM` | auto | `win32`, `x11`, `null`, … | `auto`, then try `null` |

On Windows the library always links `winhttp` (an HTTP(S) image loader in `src/core/fs.c`); the evaluation checks whether XML or theme files can make it fetch URLs and whether it can be switched off. Raven's own CI builds on `ubuntu-latest` and `windows-latest` (MSVC static and shared); its README says everything in it was tested on Windows, macOS and Linux (X11) — the project's claim, not verified here.

## Isolation

- **Source:** a separate checkout of the pinned commit (`git fetch --depth 1 origin 7ab270df…` into a folder outside the repository), passed to the prototype's CMake as a path. Not a submodule, not `FetchContent`, not a Conan package, never a moving branch.
- **Target:** `sandbox/raven-evaluation/`, added only when explicitly enabled (an option that defaults to OFF, plus the path to the Raven checkout). The default presets, CI, the engine, island-preview and the Dear ImGui panel stay unchanged; a normal configure never touches Raven or the network.
- **Code:** the prototype links the engine as it is (platform, renderer, WorldGen) and adds its own OpenGL backend for Raven and its own SDL3 → Raven input translation in the sandbox folder. If the prototype needs an engine change (for example a render target), it stops and reports instead of changing the engine.
- **Branch (proposed, needs the user's confirmation; `docs/DEVELOPMENT_WORKFLOW.md` only defines milestone branches):** `experiment/raven-ui-evaluation`, created from `milestone/m4-scene-data-model` at the M4 plan commit. The prototype's commits stay there. The filled-in report (this file) and, for PASS or CONDITIONAL PASS, an ADR are brought into the M4 branch with one `docs(experiments)` commit; the prototype code reaches `main` only if a later ADR adopts Raven.

## Evaluation plan

Each item is checked on Windows 11 + MSVC first (main platform); Linux + GCC only if the prototype and the environment allow it, reported separately. Every result is a command and an observation; a check that was not run is reported as not run.

1. **Build** (task §12.3): Raven at the pinned commit with the options above, as a C11 static library without project warning flags (like GLAD and stb); confirm from the configure log that nothing is downloaded and that the Vulkan headers are not looked up.
2. **Prototype shell** (§12.4): one SDL3 window from the engine, Raven on the host platform; an XML layout with a top toolbar, the main viewport, a hierarchy panel (TreeView), an inspector (TextInput, Button), a console (TabView, ListView) in a DockArea; the base theme and a style change.
3. **SDL3 input** (§12.5): mouse move, buttons, wheel, keys with modifiers and repeat, text input (UTF-8 from SDL decoded into code points), focus lost and regained, window resize, display scale, mouse leaving the window, close; mouse capture; shortcuts; no event handled twice (UI vs camera, as `wantsMouse`/`wantsKeyboard` do for ImGui); one SDL window and one loop.
4. **OpenGL backend** (§12.6): the instance shading ported to GLSL 4.50 core; textures (RGBA8, ALPHA8 glyph atlas, filters); clipping and scissor; straight-alpha blending; text and fonts; resize; the engine's OpenGL state unchanged after the UI (checked like the ImGui backend, ADR 0018); `disable_render_thread` on the context thread; no OpenGL call from another thread. A shared context is used only with a written justification and its own check.
5. **3D viewport** (§12.7): the generated archipelago drawn by island-preview's world scene inside the Raven viewport, first through the draw callback, then (if needed) through the texture mode with a CPU readback; frame time and latency of the readback measured and compared with the direct path; resize callback, realtime mode, viewport size. A UI drawn over an otherwise unchanged 3D window does not count as viewport integration.
6. **Threading and stability** (§12.8): widget deletion, window close, engine stop, `Rshutdown` and `Rinit` again, regenerating the world while the UI runs; Raven's allocation count at 0 after shutdown, the renderer's resource counts level, no deadlock, no hang, no crash in Debug and Release.
7. **Editor fitness** (§12.9): docking, hierarchy, inspector, console, viewport, text editor, file browser, themes, shortcuts, high DPI, focus — each rated against M5's needs, with what is missing.

## Decision criteria

- **PASS:** every mandatory check holds on Windows with MSVC: the build downloads nothing and leaves the engine's build untouched; the OpenGL backend draws the UI correctly (text, clipping, blending, textures, resize); the archipelago is drawn in the viewport with UI on top at an acceptable frame time; SDL3 input works completely without double handling; everything runs on the context thread; shutdown and re-initialization leak nothing. Raven can then be proposed as M5's UI framework in an ADR.
- **CONDITIONAL PASS:** the mandatory checks hold with limits that need their own decision (for example no custom shader effects, readback-only viewport, a missing widget, Linux not verified, API instability).
- **FAIL:** a mandatory check fails without a reasonable fix inside the prototype, or integration needs changes to the engine's architecture; Dear ImGui stays, the M3 WorldGen panel is not rewritten, and the problems are documented here.

A successful compile alone is never a PASS. Even after a PASS, Dear ImGui is not removed: the ADR describes the reasons, the integration architecture and Raven's role in M5, and the debug UI stays on Dear ImGui until a migration is accepted.

## Report

Filled in by the evaluation; until then every entry is "not run".

| Section | Result |
|---|---|
| Tested Raven commit | `7ab270df3b618de3d11af6033912374b6cd621b7` (pinned; not built yet) |
| Raven version | 0.2.3 (from its CMake project) |
| License | Apache 2.0 (from its `LICENSE`, `NOTICE`, `THIRD_PARTY.md`) |
| Build configuration | not run |
| Dependencies | not run |
| Architecture | not run |
| SDL3 input integration | not run |
| OpenGL backend | not run |
| Viewport integration | not run |
| Threading | not run |
| Performance observations | not run |
| Stability | not run |
| Limitations | not run |
| Screenshots | none |
| Windows results | not run |
| Linux results | not run |
| Comparison with Dear ImGui | not run |
| Conclusion (PASS / CONDITIONAL PASS / FAIL) | not decided |
