# ADR 0019: Editor UI after the Raven evaluation

- **Status:** Accepted (2026-11-15), option 2: Raven is the interface of the M5 editor; Dear ImGui stays the debug UI and the fallback. Proposed on 2026-11-14 and accepted preliminarily the same day; the user accepted it after the final validation (see *Acceptance*). The engine itself does not change before M5.
- **Related:** ADR 0018 (Dear ImGui as the debug UI), `docs/TECH_STACK.md` §5, `docs/ENGINE_PLAN.md` §9 and M5, `docs/experiments/raven-ui-evaluation.md`, `docs/experiments/raven-final-validation.md`, AGENTS.md §3 and §7

## Context

`docs/TECH_STACK.md` §5 chose Dear ImGui for the first editor and allowed Raven to replace it only after a prototype proves a real advantage with a working viewport. The evaluation (`docs/experiments/raven-ui-evaluation.md`) built Raven 0.2.3 at commit `7ab270df3b618de3d11af6033912374b6cd621b7` into an isolated prototype on the engine's SDL3 window, OpenGL 4.5 context and renderer, on Windows with MSVC. Its result is a **conditional pass**: the interface, the 3D viewport (the engine's renderer drawing from Raven's draw callback), SDL3 input, threading and repeated start/stop all work, at about 0.1 ms of UI cost per frame; but Raven has no OpenGL backend of its own, its host platform has gaps, its API is not stable, and Linux was not verified.

## Options

1. **Keep Dear ImGui for the M5 editor.** Known integration (ADR 0018), Conan package, stable API. The editor's panels, docking and look are built on immediate-mode widgets; docking needs Dear ImGui's docking branch or a layout of our own; a 3D viewport inside a window needs a render target or a draw callback.
2. **Raven for the M5 editor's interface, on conditions; Dear ImGui stays the debug UI.** Retained widgets from XML with docking, trees, tabs, text editor, file browser, dialogs and themes; the viewport works with the renderer as it is. Costs: a project-owned OpenGL backend and SDL host bridge, an engine event hook, a dependency outside Conan Center with an unstable API.

## Proposed decision

Option 2, **if** the user accepts the conditions below; otherwise option 1.

### Reasons

- The editor shell M5 needs (viewport, hierarchy, inspector, console, world generation panel, docking) came out of a 52-line XML file with no widget code, and looked finished (`docs/experiments/raven-ui-evaluation/001-editor-shell.png`).
- The viewport draws through the engine's renderer without a render-target API and without read-back; the UI costs about 0.1 ms per frame on top of the world.
- Input, focus, text, layouts and resizing went through SDL3 without double handling; the OpenGL state was restored exactly; Raven's memory returned to zero after every shutdown.

### Conditions (each needs the user's yes)

1. **Ownership of the integration code:** the project maintains the OpenGL 4.5 backend (about 900 lines) and the SDL3 host bridge (about 550 lines), and re-checks them on every Raven update; custom shader effects stay unavailable on OpenGL 4.5.
2. **Packaging:** Raven is taken as pinned source (vendored under `third_party/raven/` with its `LICENSE` and `NOTICE`, or a project Conan recipe — decided in the M5 step that adds it), compiled without the project's warning flags, with Vulkan, web pages, examples, tests and tools off, and updated only by a reviewed commit.
3. **API instability:** Raven 0.2.x may change; the editor's use is kept behind the editor's own panel code so an update touches few files.
4. **Host-platform rules:** panels float inside the window only (no torn-off OS windows); the host releases held input on focus loss; no IME positioning until Raven offers it; the viewport's first size is queried.
5. **Features reviewed before the editor ships:** live reload of XML and theme files, auto-loading from search paths, the HTTP(S) image loader linked on Windows.
6. **Verification still owed:** a Linux build and run, and a display scaled above 100 %, in the first M5 step that integrates Raven; failing either reopens this decision.

### Integration architecture, if accepted

Mirrors ADR 0018, so no OpenGL type leaves the renderer boundary and no SDL type the platform layer (AGENTS.md §3):

- **Backend:** the prototype's backend moves to `engine/src/render/opengl/` (the only place with OpenGL), behind internal functions; the SDL3 host bridge to `engine/src/platform/sdl3/`.
- **Engine loop:** an internal hook in `Platform::pollEvents` hands raw events to the UI (as `forwardEventToDebugUi` does for Dear ImGui), and the frame gets one UI draw point between `Renderer::beginFrame` and `endFrame`; Raven runs with `RInitDesc::disable_render_thread` on the main thread, which owns the OpenGL context. Raven's global state (one `Rinit` per process, callbacks without user data) is owned by one engine object.
- **Editor:** the editor application (`editor/`, M5) builds its panels with Raven's C API and XML layouts; the 3D viewport uses the draw callback; documents, commands, selection and undo stay the editor's own (Raven is only the interface).
- **Dear ImGui stays** for island-preview's WorldGen panel and the engine's debug UI until a separate migration is accepted. The editor application itself uses one UI stack (`docs/TECH_STACK.md` §5).

## Preliminary acceptance (2026-11-14)

The user chose option 2: Raven is the preferred interface of the M5 editor, conditionally. Until this ADR is accepted:

- Dear ImGui stays: the WorldGen panel of island-preview is not rewritten and the debug UI is not removed.
- Raven does not enter the production engine in M4.

Before M5, in addition to the conditions above:

1. Build and run on Linux.
2. Run on a real display scaled above 100 %.
3. Check the clipboard and the text widgets the editor needs.
4. Check the behaviour on focus loss.
5. Check the limits of the host platform.
6. Confirm that the project can safely maintain its own OpenGL backend and SDL3 bridge.

If the Linux or High-DPI check finds a critical problem, the choice of UI is reopened and Dear ImGui is considered again.

## Acceptance (2026-11-15)

The final validation (`docs/experiments/raven-final-validation.md`, branch `experiment/raven-final-validation`) did the six checks above and ended with **APPROVED**. The user accepted this ADR on 2026-11-15. What it found:

1. **Linux:**
   - The prototype builds with GCC 13 (warnings as errors) in Debug and Release.
   - It runs on X11 (Xvfb, Mesa's OpenGL 4.5, software rendering) with the self-test, the stress test and real input passing.
   - Real graphics hardware and a desktop session were not available.
2. **Displays above 100 %:**
   - Windows set to 150 %, and changed between 100 and 150 % while the program ran.
   - Linux at 150 % through `Xft.dpi`.
   - Layout, text and clicks follow the scale, to the pixel.
3. **Clipboard and text:**
   - Typing with key presses and keyboard layouts, Cyrillic included.
   - The editing keys.
   - Ctrl+A, C, X and V with the system clipboard, in both directions.
4. **Focus loss:**
   - Keys and buttons held across a real focus loss are released.
   - The focused widget gets its keyboard back.
5. **Host platform limits:** as recorded in condition 4. Raven also offers the caret rectangle (`Rwindow_get_text_input_rect`) that IME positioning needs.
6. **Maintenance:** four problems were found, all in the project's own integration code, none in Raven. Each fix was small and local, which supports condition 1.

**What carries into M5.** The first M5 step that brings Raven into the engine does the following:

1. **Linux on real graphics hardware and a desktop session** (X11 with a window manager, Wayland). A critical problem there reopens this decision.
2. **The validation's fixes**, taken over by the integration in `engine/src/platform/sdl3/` and `engine/src/render/opengl/`:
   - held input released on focus loss;
   - one close request for SDL's close request and quit;
   - no use of a window's handles after an accepted close;
   - the clear colour restored;
   - the wider OpenGL state comparison.
3. **IME positioning:** the host gives `Rwindow_get_text_input_rect` to `SDL_SetTextInputArea`. The "no IME positioning until Raven offers it" of condition 4 no longer applies.
4. **Conditions 1–5 stand:**
   - Raven is taken as pinned source with its notices.
   - The unstable 0.x API stays behind the editor's own panel code.
   - Panels float inside the window only.
   - Live reload, search-path loading and the HTTP image loader are reviewed before the editor ships.

## Consequences

- Accepted: one new dependency (Apache 2.0, with notices), about 1 500 lines of integration code owned by the project, an internal engine hook added in M5, Linux and scaled-display checks in M5's first Raven step.
- Rejected: M5 uses Dear ImGui; the prototype stays on the branch `experiment/raven-ui-evaluation` as a reference, and this ADR records why.
- Either way, nothing in M4 (Scene, ECS, Assets, Scripting) depends on this decision.
