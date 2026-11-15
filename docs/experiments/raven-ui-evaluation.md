# Raven UI evaluation

- **Status:** done on Windows (2026-11-14). **Conclusion: CONDITIONAL PASS** (see *Conclusion*). Linux was not run.
- **Purpose:** decide whether Raven can be the UI framework of the M5 editor instead of Dear ImGui (`docs/TECH_STACK.md` §5, `docs/ENGINE_PLAN.md` §9). Dear ImGui stays the working debug UI (ADR 0018) whatever the result, until a separate, accepted migration.
- **Not part of M4:** no M4 step depends on it, it is not an M4 exit criterion, and it changed no Scene, ECS, Assets or Scripting code (`docs/milestones/m4-scene-data-model.md`). The decision it prepares is ADR 0019 (proposed).
- **Prototype:** `sandbox/raven-evaluation/` on the branch `experiment/raven-ui-evaluation` (created from `milestone/m4-scene-data-model` at the M4 plan commit `12ba63e`); how to build and run it: its `README.md`. Screenshots: `docs/experiments/raven-ui-evaluation/`.

## Subject

| | |
|---|---|
| Repository | <https://github.com/oscar-soirey/Raven> |
| Tested commit | `7ab270df3b618de3d11af6033912374b6cd621b7` (2026-11-14, "docs: add Trendshift daily and weekly badges"), fetched by `sandbox/raven-evaluation/fetch_raven.py`, which checks the hash |
| Version | 0.2.3 (`project(raven VERSION 0.2.3 …)`; `Rget_version()` at run time) |
| Language | C11 (`C_STANDARD 11`); Objective-C only on Apple platforms |
| License | Apache License 2.0, with a `NOTICE` file; bundled: stb_truetype 1.26 (modified, public domain or MIT), stb_image 2.30 (public domain or MIT), nanosvg (zlib), Roboto (Apache 2.0), Roboto Mono (OFL 1.1) — from its `THIRD_PARTY.md` |
| API stability | its README: "still in active development and public API is subject to change" |

## Static inspection at the tested commit

Read: `README.md`, `docs/ARCHITECTURE.md`, `docs/XML.md` (DockArea slots), `CMakeLists.txt`, `cmake/`, `include/raven/raven_core.h`, `raven_host.h`, `raven_backend.h`, `raven_viewport.h`, the widget, control, layout, shell, style and XML headers for the functions used, `src/platform/host/host_platform.c`, `src/render/vulkan/shaders/ui.vert` and `ui.frag`, `src/render/webgl/` (for the shader differences), `examples/editor`, `examples/workspace`, `examples/viewport_opengl`, `THIRD_PARTY.md`. Not read: `docs/THEMES.md`, `examples/bevy_editor`, `examples/components` (not needed for the checks below).

- **Rendering backends:** Vulkan (default), WebGL 2 (Emscripten only) and null; **no desktop OpenGL backend**. An application backend is a `RBackendVTable` (`Init`, `Shutdown`, `CreateSurface`, `DestroySurface`, `CreateTexture`, `UpdateTexture`, `SetTextureFilter`, `DestroyTexture`, `RenderFrame`, `WaitIdle`, optional `CreateShader`/`DestroyShader`) given to `Rbackend_register` before `Rinit(R_BACKEND_CUSTOM)`. The UI is a flat array of 80-byte `RDrawInstance`s shaded by signed distance, plus a short command list. `CreateShader` takes SPIR-V, which OpenGL 4.5 Core cannot load (`GL_ARB_gl_spirv`), so custom shader effects are not available on this backend; Raven then draws those primitives as plain rectangles. The table has no user-data pointer, and Raven allows one `Rinit` per process.
- **Threads:** by default every backend call runs on a dedicated render thread; `RInitDesc::disable_render_thread` runs them inline on the UI thread (the thread that called `Rinit`). A job system with one worker per spare core paints large trees and decodes textures.
- **Loop:** none of its own; `Rdraw_frame()` once per frame. With the render thread disabled the backend's `RenderFrame` (and the viewport draw callbacks in it) run inside that call.
- **Host platform:** `Rhost_platform_register` before `Rinit`; Raven opens no window and reads no OS events; the application gives sizes and scale and injects input; Raven asks for the cursor, the clipboard and wake-ups. No native child windows, so viewports use the texture or draw-callback mode. The host description has no callback for creating further OS windows and none for text-input areas (IME).
- **CMake:** the only download in Raven's CMake files (`FetchContent` of Khronos Vulkan-Headers in `cmake/RavenVulkanHeaders.cmake`) is included only inside `if(RAVEN_BACKEND_VULKAN)`. `RAVEN_BUILD_WEB` defaults to ON and compiles pages that drive an installed Chromium-based browser. On Windows the library always links `winhttp` (an HTTP(S) image loader in `src/core/fs.c`). XML files created from disk are watched and reloaded when they change (live reload, on by default; `Rxml_set_live_reload`, `RAVEN_LIVE_RELOAD=0`).

## Build configuration and isolation

- **Source:** a separate checkout of the tested commit in `build/external/raven` (ignored by Git), made by `fetch_raven.py`; not a submodule, not `FetchContent`, not a Conan package.
- **Target:** `sandbox/raven-evaluation/`, added by the root `CMakeLists.txt` only with `-DLPE_BUILD_RAVEN_EVALUATION=ON` (default OFF; the presets, the `.bat` wrappers and CI never set it). The prototype configures Raven with `RAVEN_BACKEND_VULKAN=OFF`, `RAVEN_FETCH_VULKAN_HEADERS=OFF`, `RAVEN_BUILD_EXAMPLES=OFF`, `RAVEN_BUILD_TESTS=OFF`, `RAVEN_BUILD_TOOLS=OFF`, `RAVEN_BUILD_WEB=OFF`, `RAVEN_BUILD_SHARED=OFF`, `RAVEN_WAYLAND=OFF` (normal variables; Raven's `cmake_minimum_required(3.16)` makes `option()` respect them), `add_subdirectory(… EXCLUDE_FROM_ALL SYSTEM)`, in separate build folders (`build/raven-eval-debug`, `build/raven-eval-release`) configured from the normal presets with `-B`.
- **Downloads:** the configure log reports `Raven 0.2.3: platform=win32 vulkan=OFF webgl=OFF shared=OFF` and nothing else from Raven; no `_deps` folder exists in the build tree; nothing was fetched.
- **Warnings:** the prototype's own code builds with the project's warning flags and warnings as errors, without warnings. Raven's sources, built with Raven's own `/W4` (not as errors), give 9 MSVC warnings (C4132, C4310, 2× C4389, 3× C4701, 2× C4702) in `file.c`, `heap.c`, `nanosvg.h`, `style.c`, `theme.c`, `combo_box.c`, `text_editor.c`, `pdf_view.c`; adopted, Raven would be compiled like GLAD and stb, without the project's flags.
- **Engine untouched:** no file under `engine/`, `tests/`, `sandbox/island-preview/` or `assets/` changed. The normal build with the option off was checked: `scripts\windows\build.bat debug` (warnings as errors) passed, `scripts\windows\test.bat debug` passed 299/299, `scripts\windows\format-check.bat` reported 152 files, 0 to format.

## Architecture of the prototype

```text
SDL3 window + OpenGL 4.5 context (one window, one loop, main thread)
 ├─ engine::render::Renderer         the archipelago (island-preview's WorldScene, sky)
 ├─ SdlRavenHost                     SDL events -> Rhost_inject_*, size/scale -> Rhost_window_set_size,
 │                                   cursor and clipboard through SDL
 └─ Raven (host platform, R_BACKEND_CUSTOM, disable_render_thread)
     └─ GlRavenBackend               RBackendVTable on the current context:
         ├─ Raven's ui.vert/ui.frag read from the checkout and adapted at Init
         ├─ RenderFrame: save GL state -> UI state -> instanced draws -> restore
         ├─ R_DRAW_CALLBACK: restore the engine's state, viewport + scissor = widget,
         │                   call the application, back to UI state
         └─ R_DRAW_BACKDROP: copy the frame, two blur passes (glass)

per frame: poll SDL -> host -> camera -> Renderer::beginFrame -> Rdraw_frame()
           (UI + viewport callback draws the world) -> Renderer::endFrame -> swap
```

- **Shader adaptation:** push constants become a uniform struct, `gl_VertexIndex` becomes `gl_VertexID`, `gl_FragCoord` is redeclared with `origin_upper_left` (Vulkan's convention), the descriptor sets become texture units 0–2, the clip-space Y sign is handled by the uniforms, and backdrop reads flip V (the copy of a bottom-up framebuffer). Each change is a text replacement that must match, so a changed shader upstream fails `Init` with a message instead of drawing wrongly.
- **OpenGL state:** textures use direct state access, so `CreateTexture`/`UpdateTexture` (which Raven also calls outside frames) change no binding; pixel-store state is saved and restored around uploads. `RenderFrame` captures 30 pieces of state (program, vertex array, buffers, texture and sampler bindings of units 0–2, framebuffers, viewport, scissor, blend, depth, cull, stencil, colour mask, pack/unpack) and restores them; a capture after the restore must equal the one before, otherwise a "state mismatch" is counted.
- **Glyph masks:** `R_TEXTURE_ALPHA8` becomes `GL_R8` with the swizzle (1, 1, 1, R); no shader change.
- **Windows:** the backend draws only the Raven window the host presents; frames Raven produces for other windows are counted and skipped (see *Limitations*).
- **Size:** backend 924 lines (source and header), host bridge 535 lines, program with self-test, stress test and benchmark 1727 lines (after clang-format), layout 52 lines of XML.

## Results on Windows

Machine: Windows 11 (10.0.26100), MSVC 19.51 (Visual Studio 2026, CMake 4.5.0-rc3), NVIDIA GeForce RTX 4060, driver 610.88, display scale 100 %. SDL 3.4.16 and the engine as on `milestone/m4-scene-data-model`. Debug and Release builds, both with the project's warnings as errors.

| Check | Command | Debug | Release |
|---|---|---|---|
| Self-test, dark look (23 steps: layout, input, focus, resize, tabs, docking, floating, viewport modes, shortcuts, close) | `raven-evaluation --self-test` | 23/23 pass | 23/23 pass |
| Self-test, glass look | `raven-evaluation --self-test --look glass` | 23/23 pass (2674 backdrop copies) | — |
| Stress: Raven started and stopped with regeneration, resize and both viewport modes per cycle | `raven-evaluation --stress N` | 5/5 cycles pass | 10/10 cycles pass |
| Benchmark | `raven-evaluation --benchmark` | — | see *Performance* |
| Screenshots | see `docs/experiments/raven-ui-evaluation/README.md` | — | 5 images |

In every run: OpenGL errors 0, state mismatches 0, backend calls from a wrong thread 0, renderer errors 0, Raven errors and warnings 0, Raven's allocation count after `Rshutdown` 0, backend textures after `Shutdown` 0.

### SDL3 input integration

Every event the self-test sends is pushed into SDL's own queue (`SDL_PushEvent`) and goes the same way as real input: `SDL_PollEvent` → `SdlRavenHost` → `Rhost_inject_*` → Raven's widgets → the prototype's callbacks.

| Input | Result |
|---|---|
| Mouse move, buttons, click | a toolbar button clicked (Regenerate: new world, renderer resources level); TreeView row clicked and walked with arrow keys (inspector filled for island 2); TabView tabs switched |
| Right-drag in the viewport | the camera turned (yaw 0.652 → 0.452 rad) through the viewport's event callback (`Rwidget_set_event_callback`) |
| Wheel | over the viewport: flying speed 30 → 46.9 m/s; over the hierarchy: speed unchanged (the tree got it) |
| Keys | W held in the focused viewport flew the camera 2.8 m; F12 shortcut saved a screenshot |
| Shortcut with a non-Latin layout | Ctrl+R sent with keycode 'к' (Cyrillic) and scancode R regenerated the world: the host maps Latin keys by meaning and others by position |
| Text input | "Остров α" typed as UTF-8 (`SDL_EVENT_TEXT_INPUT` → code points → `Rhost_inject_text`) reached the TextInput and, through its changed callback, the tree item |
| No double handling | while the TextInput had the keyboard, "w" went into the text and the camera did not move (0.000 m) |
| Window focus | `Rhost_inject_focus(false)` reached Raven (popups and captures end), but **the focused viewport got no `R_EVENT_FOCUS_LOST` and no key-up**: the host must release held keys itself (the prototype does; the camera then stayed still, 0.000 m) |
| Mouse leaving the window | `Rhost_inject_mouse_leave` delivered, no error |
| Resize | `SDL_SetWindowSize` → pixel size → `Rhost_window_set_size`; layout and viewport followed (790×503 → 665×444 px), one resize callback. **The resize callback is not called for the first size**; the initial size is read with `Rviewport_get_pixel_size` |
| Scale | `Rhost_window_set_size(…, scale)` from `SDL_GetWindowDisplayScale`; checked only at 100 % and with `RInitDesc::ui_scale` 1.5 (crisp text and layout, `005-ui-scale-150.png`), not on a display set to another scale |
| Close | `SDL_EVENT_WINDOW_CLOSE_REQUESTED` → `Rhost_inject_close` → the window's close callback → `Rdraw_frame` returns false → the loop ends |
| Cursor, clipboard | cursor changes through `SDL_CreateSystemCursor` (2 in the self-test); clipboard callbacks wired to SDL, not exercised by the test |
| One window, one loop | yes: Raven opened no window and read no OS events; the prototype has one SDL window and one loop |

### OpenGL backend

- Raven's whole interface in this layout draws correctly through the backend: text (glyph atlas), rounded rectangles and outlines, tab underlines, tree arrows, the scroll bar, clipping (cut tree labels, the console's scroll area), straight-alpha colours blended as premultiplied, the dark and glass looks (`001`, `003`), the viewport texture (`002`). Resize and UI scale 1.5 keep it sharp (`005`). The light and Windows looks were not looked at.
- The engine's renderer keeps working after and inside Raven's frame: 0 renderer errors, 0 OpenGL errors, the archipelago draws with depth, culling and blending as in island-preview.
- `disable_render_thread`: every backend call came from the thread that created the context (0 calls from another thread). No shared context was needed.
- The glass look works with backdrop copies of the whole frame and two blur passes; Raven asked for 11–12 copies per frame in this layout. Copying only the requested rectangle (`RDrawCommand::rect`) would be the obvious improvement.

### Viewport integration

- **Draw callback (main path):** the generated archipelago (seed 1, 12 islands, 172 264 triangles, water, clouds, sky) is drawn by the engine's renderer from `Rviewport_set_draw_callback` inside the widget: the backend restores the engine's OpenGL state, sets viewport and scissor to the widget, calls back, and returns to the UI state. UI can be drawn over it; the scene is clipped by the widget (`001`).
- **Texture with CPU read-back (comparison):** the world is drawn into the corner of the frame, read back with `glReadPixels`, flipped and given to `Rtexture_update`; identical picture (`002`). It costs a pipeline stall (*Performance*). A render-to-texture path without read-back would need a render-target API the engine does not have yet.
- `Rviewport_set_realtime`, `Rviewport_get_pixel_size` and `Rviewport_set_resize_callback` work as documented, except that the resize callback does not report the first size.

### Threading and stability

- UI thread = the context thread; Raven's render thread disabled; Raven's workers exist (job system) but never call the backend; Raven's `wake` callback came about once per frame (from the realtime viewport) and is thread-safe (an atomic counter).
- 10 Release and 5 Debug start/stop cycles of Raven (`Rinit_ex` … `Rshutdown`), each with two regenerations of the world, a window resize and a viewport-mode switch: all memory of Raven returned (allocation count 0), all backend textures released, the renderer's meshes always exactly the world's needs plus the sky, backend `Init`/`Shutdown` balanced (10/10).
- Tree items deleted and recreated on every regeneration, the window closed through its close callback, a second (floating) window created and docked back, `Rshutdown` with all widgets alive, and the engine stopped after it: no errors; no deadlock, hang or crash in any run (several thousand frames in total, Debug and Release).
- Raven keeps global state (one `Rinit` per process; the backend table and the error callback have no user data); the prototype keeps the backend's state in one object of its own.

### Performance

Release, 1280×760 window, 790×503 viewport, `--benchmark` (vsync off, `glFinish` after every frame so that GPU work is counted, 600 frames per row after 30 warm-up frames):

| Configuration | Frame (CPU + GPU) | `Rdraw_frame` (CPU, includes the world drawn in the callback) | Backend `RenderFrame` | Notes |
|---|---|---|---|---|
| World alone, whole window, no UI | 0.237 ms | — | — | island-preview's drawing without Raven |
| Editor, draw-callback viewport, dark look | 0.330 ms | 0.074 ms | 0.046 ms | 656 instances, 3 draw calls, 29 widgets; layout 0.007 ms, paint 0.026 ms |
| Editor, CPU read-back viewport, dark look | 1.447 ms | 0.067 ms | 0.029 ms | read-back 0.569 ms + upload 0.413 ms per frame (stall) |
| Editor, draw-callback viewport, glass look | 1.114 ms | 0.446 ms | 0.406 ms | 12 full-frame backdrop copies per frame |

The interface costs about 0.1 ms per frame on top of the world in the draw-callback mode. The read-back mode is about four times slower than the callback here and would grow with the viewport; it is not suitable as the editor's main path. Startup: the window from `editor.xml` in 20–37 ms.

## Linux results

Not run: no Linux desktop with a GPU was available for this evaluation, and CI does not build the prototype (it is opt-in and needs a window and OpenGL). The prototype's code has no Windows-only part (SDL3, OpenGL, Raven's X11 platform is compiled but not used with the host platform), but a Linux build is unverified.

## Limitations found

1. **No upstream OpenGL backend.** The project would own about 900 lines of backend and adapt Raven's shaders on every Raven update (adaptation fails loudly when a pattern changes). Custom shader effects (SPIR-V) are not possible on OpenGL 4.5.
2. **Host platform gaps:** no way to create further OS windows, so torn-off or natively floating panels are invisible (Raven creates a second window with a surface but the host cannot show it; the prototype skips its frames); in-window floating works. No text-input area/IME callback. Window focus loss is not passed to the focused widget as focus-lost or key-up events. The viewport resize callback does not report the first size. A programmatic `Rtree_view_set_selected_item` does not call the selection callback (documented behaviour of many toolkits; the application updates itself).
3. **Engine integration:** the prototype owns SDL and the loop because the engine's `Platform` hides SDL events. Inside `engine::app::run`, Raven would need the same kind of internal event hook and UI draw point as Dear ImGui has (ADR 0018) — an engine change.
4. **API stability and packaging:** version 0.2.x with an API "subject to change"; not in Conan Center; Apache 2.0 with a `NOTICE` to carry; Raven compiles with 9 MSVC warnings at `/W4`.
5. **Features to review before shipping an editor on it:** live reload of XML and theme files is on by default; on Windows an HTTP(S) image loader is always linked (`winhttp`); auto-loading of documents from search paths is on by default.
6. **Not verified:** Linux; a display with a scale other than 100 %; the clipboard path; the text editor, file browser, dialogs and accessibility widgets; hours-long sessions.

## Comparison with Dear ImGui

| Aspect | Dear ImGui (as in ADR 0018) | Raven 0.2.3 (this prototype) |
|---|---|---|
| Integration with SDL3 + OpenGL | official backends from the Conan package, small internal glue | project-owned OpenGL backend (~900 lines) and SDL host bridge (~550 lines) |
| Package and license | Conan Center `imgui/1.92.9b`, MIT | not in Conan Center; pinned source; Apache 2.0 + NOTICE |
| Model | immediate mode; the application keeps all state | retained widget tree from XML, callbacks, live reload, themes and looks |
| Editor widgets | basic widgets; docking only in the docking branch (not used) | DockArea (drag, tabs, save/load), TreeView, TabView, TextInput, ListView, TextEditor, FileBrowser, dialogs, accessibility |
| 3D viewport | `ImGui::Image` needs a render target; draw callbacks possible | draw callback works with the engine's renderer as it is |
| Threads | single-threaded | job system; render thread (disabled here) |
| Cost per frame | not measured in this evaluation | about 0.1 ms over the world (dark look) |
| Maturity | widely used, stable API | young, API subject to change |
| Look | developer tool | polished editor look out of the box (`001`, `003`) |

## Conclusion

**CONDITIONAL PASS.** On Windows with MSVC every mandatory check holds: Raven builds from a pinned source without downloads and without touching the engine's build; a project-owned OpenGL 4.5 backend draws the whole interface correctly on the engine's context and leaves the OpenGL state as it found it; the generated archipelago is drawn by the engine's renderer directly in Raven's viewport with UI around it at about 0.1 ms of UI cost per frame; SDL3 input (mouse, wheel, keys, layouts, UTF-8 text, focus, resize, close) works through the host platform without double handling; everything runs on the context thread; Raven can be started and stopped repeatedly without leaks. The limits above need their own decision before M5 builds on it: maintaining the backend and the host bridge, the host platform's gaps (floating windows, IME, focus), the engine's event hook, packaging and API instability, the features to turn off, and the unverified Linux and scaled-display behaviour.

Decision gate: ADR 0019 (proposed) describes the reasons, the integration architecture and Raven's role in M5 if the conditions are accepted. Dear ImGui is not removed: the M3 WorldGen panel and the debug UI stay on Dear ImGui until a separate migration is accepted.
