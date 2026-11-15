# ADR 0027: Raven in the engine

- **Status:** Accepted (2026-11-15, M5.2). The plan asked M5.2 to decide this ADR (`docs/milestones/m5-minimal-editor.md`, M5.2, "Decisions to make"); the decisions follow ADR 0019's integration architecture and were taken under `docs/DEVELOPMENT_WORKFLOW.md` §4.
- **Related:**
  - ADR 0019 (Raven for the editor: its integration architecture, conditions and validated fixes);
  - ADR 0026 (Raven packaging: the vendored copy, live reload and auto-loading off);
  - ADR 0018 (Dear ImGui as the debug UI);
  - ADR 0002 (SDL3 owns the windows), ADR 0006 (frame capture);
  - `docs/experiments/raven-final-validation.md`;
  - AGENTS.md §3 and §4.

## Context

M5.1 made Raven a pinned static library (ADR 0026). M5.2 has to host it in the engine.

- **What the engine has:** one SDL3 window, one OpenGL 4.5 Core context on the main thread, a renderer with `beginFrame` and `endFrame`, and Dear ImGui as the debug UI, which reaches the platform layer through `forwardEventToDebugUi`.
- **What the validated prototype shows** (branch `experiment/raven-final-validation`):
  - Raven runs well on the host platform with a project-owned OpenGL backend, with its render thread disabled.
  - The validation found four fixes the integration must keep: held input released on focus loss, one close request for SDL's close and quit, no handle used after an accepted close, and the clear colour restored with a wider state comparison.
- **What the plan asks:** a public way for an application (the editor) to turn Raven on and reach its C API, without Raven, SDL or OpenGL types in the engine's other headers.

## Decision

1. **Where the code lives:**
   - `engine/src/render/opengl/GlRavenBackend.*`: Raven's OpenGL 4.5 backend, from the prototype's `GlRavenBackend.cpp`.
     - It restores every state it saves, the clear colour included, and compares the state after each frame (`RavenBackendStats::stateMismatches`).
     - `sameGlStateAround()` lets other code compare the state around work of its own.
     - Raven's reference shaders `ui.vert` and `ui.frag` are embedded at build time from `third_party/raven/` (`cmake/EmbedFiles.cmake`) and adapted at Init; a changed shader fails Init.
   - `engine/src/platform/sdl3/Sdl3RavenHost.*`: the SDL3 host bridge, from the prototype's `SdlRavenHost.cpp`, with fixes 1 to 3 of the validation and the IME area (point 7).
   - `engine/src/ui/RavenUi.*`: the one owner of Raven's process-wide state. `engine/src/ui/RavenHost.hpp` is an SDL-free interface to the bridge.
   - `engine/include/engine/ui/Ui.hpp`: the public API (point 2).
   - Only `engine/src/` (these three places) and `editor/` include Raven's headers. No OpenGL type leaves `render/opengl/`, and no SDL type leaves `platform/sdl3/`.
2. **The public API** has no Raven, SDL or OpenGL type:
   - `ApplicationDesc::ui` (`std::optional<ui::UiDesc>`) turns Raven on, like `debugUi` turns Dear ImGui on. `UiDesc` holds the main window's XML layout as text (ADR 0026: the application reads it; the editor embeds its layouts at build time) and a name for messages.
   - `AppContext::ui()` returns a `ui::Ui` (null when off) with:
     - `mainWindow()`: Raven's handle as `ui::RavenId`, a plain `std::uint32_t` that equals Raven's `Rid` (checked by a `static_assert`);
     - `scale()`.
   - `RunSummary::ui` (`ui::UiRunStats`) reports the UI frames, the backend's OpenGL errors (also in `openGlErrors`), state mismatches, Raven's errors and Raven's allocations after `Rshutdown`.
   - The editor calls Raven's C API itself (`Rwidget_find`, callbacks, actions) on the main thread inside the Application callbacks. It links `raven`; the engine's public headers do not.
   - Raven's 0.x API stays behind the editor's own code (ADR 0019 condition 3).
3. **Ownership and order** (`RavenUi`, created by `engine::app::run`):
   - **Creation:** `RavenUi` is created after the window, the context and the renderer, and destroyed before them. The steps:
     1. register Raven's error callback (Raven's errors go to the log, category `raven`);
     2. register the host platform and the OpenGL backend;
     3. `Rinit_ex` with `R_BACKEND_CUSTOM` and `disable_render_thread`: Raven draws on the main thread, which owns the context;
     4. right away, `Rxml_set_live_reload(R_FALSE)` and `Rxml_set_auto_load(R_FALSE)` (ADR 0026), checked;
     5. the main window from the layout text, with continuous redraw (point 5), shown and attached to the host.
   - **Shutdown:** `RavenUi::shutdown()` runs after `Application::stop` while the renderer and the context exist. It calls `Rshutdown` (Raven destroys its windows; the backend's Shutdown releases its OpenGL objects), unregisters the host platform and counts Raven's remaining allocations.
   - **Failures:** a failure at any step undoes the earlier ones: Raven shut down if it started, host platform and error callback unregistered. The run then fails with an error naming the step (e.g. the layout's name and Raven's XML error).
   - **One per process:** only one `RavenUi` can exist (Raven allows one `Rinit`). A second one, or a Raven started outside the engine, is refused.
4. **The engine owns the main Raven window**, made from `UiDesc::layoutXml`.
   - The application finds its widgets with `Rwidget_find(mainWindow(), …)` and may set a close callback. Without one, Raven accepts a close.
   - When Raven destroys the window (an accepted close request, or `Rquit`), the loop ends. `mainWindow()` returns `kInvalidRavenId` from then on.
   - Other Raven windows, if Raven makes any, are not drawn: the engine's window shows one Raven window.
5. **The frame:**
   - The order: `pollEvents` → a check that Raven's window still exists → `Application::update` → `Renderer::beginFrame` → `Application::render` → Raven's frame (`Rdraw_frame`) → `Renderer::endFrame` → screenshot → swap.
   - Raven's frame covers the whole window: the backend clears with Raven's clear colour. What `render()` draws before it is covered. 3D content goes into a Raven viewport through its draw callback (M5.3), which runs inside Raven's frame on the engine's own state.
   - The main window is redrawn every frame (`Rwindow_set_continuous_redraw`), because the engine clears and swaps every frame. Without this, an unchanged Raven window would not be drawn again, and the window would show only the clear colour.
   - The whole `Rdraw_frame`, texture uploads included, runs inside `sameGlStateAround(…, Restored)`. A difference counts as a state mismatch, as in the validation, which also compared around Raven's whole frame.
   - While the window is minimized, Raven's frame is skipped like the rest.
6. **The event order**, in `Platform::pollEvents`:
   1. **Dear ImGui** sees every event (unchanged).
   2. **The Raven host**, while a Raven window is attached:
      - It takes every keyboard, text, mouse and wheel event, also a key Raven does not know.
      - It takes the close requests: `SDL_EVENT_WINDOW_CLOSE_REQUESTED`, and `SDL_EVENT_QUIT` on its own, become one `Rhost_inject_close`; the `SDL_EVENT_QUIT` that follows a close request is dropped. Raven's answer decides, and the loop ends when the window is gone.
      - It sees focus, size and display-scale events without taking them.
      - On focus loss it releases the keys and buttons it gave Raven, each once, modifiers last, before telling Raven the window lost the focus.
   3. **`InputState` and the quit request** get the events the UI did not take. `SDL_EVENT_WINDOW_FOCUS_LOST` always reaches `InputState::releaseAll`.

   So with the Raven UI on, `InputState` sees no keyboard or mouse input. In the editor the whole window is Raven's: input for the 3D view reaches it through the viewport widget (M5.3), so a key typed into a text field never moves a camera, and no event is handled twice. Without the UI, the order and `InputState` are as before.
7. **Text input and the IME:**
   - After every Raven frame, the host turns SDL text input on only while a Raven text field has the keyboard (`Rwindow_wants_text_input`), and off otherwise.
   - It passes Raven's caret rectangle (`Rwindow_get_text_input_rect`, in pixels) to `SDL_SetTextInputArea` in window points, when it changes.
   - **Verified:** this positions the system's IME candidate window in principle. Text arrives as committed UTF-8 (`SDL_EVENT_TEXT_INPUT`) and is decoded to code points; invalid bytes become U+FFFD.
   - **Not verified:** Raven has no composition API (`SDL_EVENT_TEXT_EDITING` is ignored), and Chinese, Japanese or Korean input with a real IME was not tested. No such support is claimed.
8. **Display scale:**
   - Raven gets `SDL_GetWindowDisplayScale` (pixel density times content scale) as pixels per logical unit, and the window's size in pixels.
   - Both are passed again on `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` and `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`, so a scale change while running needs no restart.
   - Mouse positions are SDL points times the pixel density.
9. **Dear ImGui and Raven are not combined in one application:**
   - `run()` refuses `debugUi` together with `ui` before any window exists (ADR 0019 decision 6: the editor uses Raven only).
   - island-preview and the debug UI are unchanged. They never start Raven: a GPU test checks that an application without `ui` has no Raven running.
   - Dear ImGui's renderer passes the same state comparison over all of its state.
   - The engine library links Raven's code; programs that never turn the UI on do not run it.

## Consequences

- An application turns on the editor's interface with one field. It gets Raven's main window and calls Raven directly, and the engine keeps Raven's lifetime, input, drawing and checks.
- **Tests:**
  - Headless (SDL's dummy video driver, Raven's null backend) for the host bridge, the event order and the lifetime.
  - The GPU tier for drawing, pixels, repeated runs and the state comparison.
  - The validation's four fixes have tests that fail without them.
- **Costs:**
  - About 2 250 lines of integration code owned by the project (backend, host bridge, owner, public header, comments included), re-checked on every Raven update (ADR 0019 condition 1).
  - A UI frame every frame, about 0.1 ms in the validation.
  - Raven's code is linked into every program of the engine.
- **Open:**
  - Linux on real graphics hardware is not verified in M5 (plan decision 2). CI runs Linux on software OpenGL (Mesa llvmpipe under Xvfb), which is not a hardware test.
  - Torn-off panels in OS windows are not possible (ADR 0019 condition 4).
  - IME composition with a real IME was not tested.
