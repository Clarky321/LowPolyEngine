# M5 — Minimal editor

- **Branch:** `milestone/m5-minimal-editor`, created from `main` at `1425c2c` (M4 merged through pull request #5).
- **Roadmap entry:** `docs/ENGINE_PLAN.md` §10, M5; the editor plan in §9.
- **UI decision:** ADR 0019 (accepted 2026-11-15): Raven is the editor's interface; Dear ImGui stays the engine's debug UI (ADR 0018) and the fallback. The evidence is in `docs/experiments/raven-ui-evaluation.md` and `docs/experiments/raven-final-validation.md`, both brought to this branch with the plan; the prototype's code stays on `experiment/raven-final-validation`.
- **Workflow:** `docs/DEVELOPMENT_WORKFLOW.md`. One task implements exactly one sub-milestone below, then stops and reports.
- **Status of this plan:** approved by the user on 2026-11-15, with the answers to the open questions and two additions (*Decisions on the open questions*). M5.1 is a task of its own.

## Goal and exit criterion

Up to M4 the engine has a data model (entities, components, assets, scenes, prefabs, Lua) and draws a generated archipelago in island-preview. Nothing can be edited except world settings in a debug panel. M5 adds the first editor: a separate program that opens a project, shows the world and the scene in a viewport, edits the scene through panels with undo and redo, and saves it.

**Exit** (from `docs/ENGINE_PLAN.md`): create scene entities, edit transforms/components, regenerate the world, save, close and reopen. Spelled out for this plan:

1. `lpe-editor` creates or opens a project folder: the asset manifest, a scene, and the world generation settings.
2. The viewport shows the generated archipelago and the scene's entities with static meshes, with a free-fly camera. A click in the viewport selects an entity.
3. The hierarchy lists the scene's entities with their parent links and creates, deletes, renames and reparents them.
4. The inspector shows the selected entity's components and edits them: name, transform, static mesh, camera, directional light, script. It adds and removes components; invalid values are refused with the component's message.
5. The WorldGen panel edits the world settings and regenerates the archipelago in the viewport.
6. The console shows the engine's log and the document's errors and warnings.
7. Every change to the document can be undone and redone: entities, components, parent links, world settings.
8. After save, close and reopen, the scene (every ID and value) and the world settings are exactly as saved.

## Architecture principles

- **A program of its own.**
  - The editor is the executable `lpe-editor` in `editor/` (`docs/PROJECT_STRUCTURE.md`).
  - A packaged game never needs editor code; the engine never includes editor headers.
- **One UI stack in the editor: Raven** (ADR 0019, decision 6).
  - The editor builds its panels with Raven's C API and XML layouts.
  - `lpe-editor` uses Raven only: Raven and Dear ImGui are not combined in the editor's interface.
  - Dear ImGui stays for the engine's debug UI, for island-preview's WorldGen panel, and as the fallback should the project ever have to leave Raven.
- **Raven is hosted by the engine, as ADR 0019 describes:**
  - the OpenGL backend in `engine/src/render/opengl/` (the only place with OpenGL);
  - the SDL3 host bridge in `engine/src/platform/sdl3/`;
  - a hook in the main loop for raw events, and one UI draw point between `Renderer::beginFrame` and `endFrame`;
  - Raven's process-wide state owned by one engine object.
  - Scene, core, assets, scripting and worldgen never include Raven, SDL or OpenGL headers.
- **The editor changes data only through commands.**
  - Panels and the viewport never change the scene or the world settings directly: they create commands that the document applies, and that undo and redo exactly.
  - The document and the commands are UI-free and tested headless.
- **The renderer draws; it does not store the world** (M4). A new engine module `engine/world/` draws the generated world and a scene's entities with the existing renderer API. The renderer gets no render targets: the viewport is Raven's draw callback (ADR 0019).
- **Nothing that works breaks:**
  - WorldGen output (M3 golden hashes);
  - scene, prefab, manifest and settings documents (M4);
  - island-preview with its Dear ImGui panel;
  - the headless and GPU tiers.
  - Code moved out of `sandbox/island-preview/` behaves the same, and its tests prove it.
- **Headless first.**
  - Every model (project, document, commands, selection, picking, panel state) has headless unit tests.
  - Raven panels are tested headless with Raven's null backend and host platform (`R_BACKEND_NULL`; M5.1 proves this works in CI).
  - Drawing and the whole program are checked in the GPU tier and by screenshots.
- **Errors as values, explicit ownership, no globals beyond Raven's own** (AGENTS.md §4). Raven's state is per process: one engine object owns `Rinit`/`Rshutdown`, and tests never start two.

## Technology stack and dependencies

| Technology | Status before M5 | Role in M5 |
|---|---|---|
| C++20, CMake, Conan 2, Catch2/CTest, the lockfile | adopted | unchanged |
| SDL3, OpenGL 4.5, GLAD, GLM, stb, nlohmann_json, EnTT, Lua | adopted | unchanged; no new use except where a step names it |
| Dear ImGui `imgui/1.92.9b` | adopted (ADR 0018) | unchanged: the engine's debug UI and island-preview's WorldGen panel; not in the editor |
| Raven 0.2.3 at `7ab270df3b618de3d11af6033912374b6cd621b7` | accepted for the editor (ADR 0019) | added in M5.1 as pruned vendored source in `third_party/raven/` (decision 1), Apache 2.0 with `NOTICE`, compiled without the project's warning flags |

No other new dependency. Every dependency follows AGENTS.md §7 and gets a row in `docs/references/THIRD_PARTY_NOTICES.md`.

## Module structure

Planned layout; final names are chosen in the step that creates them:

```text
third_party/raven/                 Raven, pinned (M5.1)
engine/include/engine/
  ui/        the engine's Raven host: lifetime, the main window, the loop hook (M5.2)
  world/     WorldScene and Sky moved from island-preview (M5.3); the scene's
             entities for the renderer (M5.6); the world settings panel model (M5.10)
  project/   project folder and project document (M5.4); no editor, Raven, SDL3 or
             OpenGL dependency; for a runtime application later as well (decision 5)
  core/      a log listener for the console (M5.7)
engine/src/
  render/opengl/   the Raven backend (M5.2)
  platform/sdl3/   the Raven host bridge (M5.2)
editor/
  include/editor/  document/, commands/, selection/ (M5.5); panels/ (M5.7 – M5.11)
  src/             application/, viewport/, panels/, project/ (M5.2 – M5.11)
  layouts/         Raven XML (M5.2 on)
tests/unit/ui/, tests/unit/world/, tests/unit/project/, tests/unit/editor/
tests/rendering/   editor runs and pixel checks (GPU tier)
```

Dependency direction (arrows: "uses"):

```text
editor ──▶ engine: app, ui, world, project, scene, assets, render (public API)
world  ──▶ render, worldgen, scene, assets, geometry
project ─▶ scene, assets, worldgen (settings files), core
ui     ──▶ Raven (private, via render/opengl and platform/sdl3)
scene, assets, scripting, worldgen, core: unchanged; none uses ui, world, project or editor
```

## Status

| ID | Title | Status |
|---|---|---|
| M5.1 | Raven as a pinned dependency | done |
| M5.2 | Raven in the engine loop and the editor shell | done |
| M5.3 | The generated world in the viewport | done |
| M5.4 | Project folder and project document | in progress |
| M5.5 | Editor document, selection and undoable commands | planned |
| M5.6 | Scene entities in the viewport | planned |
| M5.7 | Hierarchy and console panels | planned |
| M5.8 | Inspector | planned |
| M5.9 | Picking and selection in the viewport | planned |
| M5.10 | WorldGen panel in Raven | planned |
| M5.11 | Project workflow: new, open, save, close | planned |
| M5.12 | Integration, final verification and pull request | planned |

Status values: `planned`; `in progress` (set when work starts and committed with the change); `done` (set by a separate `docs(milestones): mark M5.x done (M5.x)` commit after the change is pushed and its CI is green, `docs/DEVELOPMENT_WORKFLOW.md` §4). `git log --grep "(M5.x)"` finds the commits.

## Rules for all of M5

- **No Raven, SDL, OpenGL or Dear ImGui in the data model.** None of their headers reaches scene, assets, scripting, worldgen, core or project; a header check test enforces it. Raven's headers are included only in `engine/src/` (backend, host) and in `editor/`.
- **Engine changes only where a step names them.** WorldGen results, the M4 document formats and island-preview stay as they are; moved code keeps its tests.
- **Tests:**
  - Every new model gets headless tests: normal, boundary and failure cases.
  - Every panel gets headless Raven tests (null backend, input through the host platform).
  - Every step that draws gets a GPU-tier check.
- **Visible steps get a screenshot** in `docs/screenshots/m5/` with its command in the README there (`docs/DEVELOPMENT_WORKFLOW.md` §6), taken by the editor itself (`--screenshot`). No UI is added just for screenshots.
- **ADRs:** decisions on dependencies, public APIs, document formats and module boundaries get an ADR in the step that makes them. M5.1 took 0026 (Raven packaging), M5.2 0027 (Raven in the engine), M5.4 0028 (project document); the next free number is 0029.
- **Out of scope stays out:** gizmos, play mode, the asset browser and import, materials and prefab authoring (M6) stay out, even where Raven makes them easy.
- **The checks of `docs/TESTING_AND_CI.md` §8 for the kind of change.** The GPU tier runs from M5.2 on, since every later step touches the window, the loop or drawing.
- **Two kinds of OpenGL runs, never mixed up** (decisions 2 and 3).
  - From M5.2, CI runs the GPU tier on Linux: GitHub Actions, Ubuntu 24.04, Xvfb, Mesa llvmpipe. That is **software OpenGL**.
  - Documents and reports name it so and keep it apart from runs on real graphics hardware: locally on Windows, and on Linux not available in M5. CI does not replace a hardware test.
  - Linux on real graphics hardware is recorded as **not verified** in M5.

## Sub-milestones

### M5.1 — Raven as a pinned dependency

**Goal:** Raven builds as part of the project on Windows and Linux, from a pinned source, without downloads at configure time.

**Scope:**
- Raven as **pruned vendored source in `third_party/raven/`** (decision 1):
  - **Pinned:** version 0.2.3 at commit `7ab270df3b618de3d11af6033912374b6cd621b7`, the validated one.
  - **Kept:** `include/`, `src/`, `third_party/` (bundled stb, nanosvg and fonts with their licences), `cmake/`, `CMakeLists.txt`, `LICENSE`, `NOTICE`, `THIRD_PARTY.md`.
  - **Licences:** `LICENSE`, `NOTICE` and the licence of every bundled third-party component are kept.
  - **No downloads** during configure or build.
  - **`third_party/raven/README.md`** (the project's own) documents in detail:
    - the version and commit;
    - every kept and every removed path, with the reason;
    - any change made to Raven's files;
    - the update procedure, step by step: fetch the new commit, prune, compare, build, run the checks, and update the notices and ADR.
  - **Updates:** a Raven update is a separate, reviewable change, never part of another step.
- A CMake target `raven`, a static C library:
  - Vulkan, web pages, examples, tests and tools off (as in the prototype); Wayland as decided in the step;
  - compiled without the project's warning flags; its headers as SYSTEM includes;
  - nothing fetched (no `FetchContent`, no Vulkan headers).
- What ADR 0019 condition 5 asks about, recorded:
  - live reload and auto-loading from search paths: whether they can be switched off and how;
  - the HTTP(S) image loader (`winhttp` on Windows): a build option, or a source change recorded in the vendored README.
- `docs/references/THIRD_PARTY_NOTICES.md` (Raven and its bundled parts), `docs/TECH_STACK.md` §4/§5, `docs/PROJECT_STRUCTURE.md`.

**Decisions to make (ADR "Raven packaging"):** what exactly is pruned (vendored: decision 1), the build options, the update procedure and its review, the notices.

**Out of scope:** the engine's integration (M5.2), any editor code.

**Acceptance criteria:**
- Clean builds on all four CI jobs, warnings as errors in project code.
- No network access during configure or build.
- `LICENSE`, `NOTICE` and the licences of all bundled components are present and listed in the notices.
- The vendored README lists every kept and removed path and the update procedure.

**Unit tests (headless, `tests/unit/ui/`):**
- Raven starts with `R_BACKEND_NULL` and its null or host platform, creates a window from XML text, injects a click and text through the host platform, and reads the result.
- After `Rshutdown` Raven holds no allocation; this also proves headless Raven tests work on both CI platforms.
- Raven's version and commit are the pinned ones.

**Done when:** the target builds on all four jobs and the null-backend test passes there.

**Visual check:** none.

**Decisions made in M5.1** (ADR 0026; details in `third_party/raven/README.md`):
- **The copy:**
  - `git archive` of the pinned commit, restricted to the kept paths of the plan: 249 of 816 files, 6.2 of 36 MB.
  - Removed whole: `bindings/`, `docs/`, `examples/`, `openspec/`, `tests/`, `tools/`, `web/`, `.github/`, the root scripts and presets, `ROADMAP.md`, the Git files and upstream's `README.md`.
  - Nothing inside `include/`, `src/`, `third_party/` or `cmake/` is removed. The copy stays a plain subset of upstream, and the M5.2 backend adapts the GLSL sources in `src/render/vulkan/shaders/`.
  - `.gitattributes` exempts the vendored bytes from `git diff --check`, since upstream's whitespace is kept as it is; the project's own `README.md` there is still checked.
- **One local change.** `src/core/fs.c` compiles the HTTP(S) download out (WinHTTP on Windows, a `curl` process on Linux) under `LPE_RAVEN_NO_HTTP_DOWNLOAD`, because Raven has no build option for it. Such an image source fails like an unreadable file.
- **The target.**
  - `cmake/RavenDependency.cmake` sets the options and the pin (`LPE_RAVEN_VERSION`, `LPE_RAVEN_COMMIT`) and adds Raven's own `CMakeLists.txt` with `SYSTEM`. Raven's `CMakeLists.txt` is unchanged.
  - The target is the static C11 library `raven`, built in every configuration, with Raven's own warning level and none of the project's. C was already enabled in `project()` for GLAD. Conan and the lockfile are unchanged.
- **Wayland and X11: `RAVEN_PLATFORM=null`.**
  - Only Raven's null and host platforms are built, so there is no X11, Wayland or Win32 window code and no X11 build dependency for Raven.
  - SDL3 owns the windows, and Raven reaches them through the host platform, which Raven picks whenever one is registered. The validation ran on the host platform too.
- **Live reload and auto-loading** are run-time switches (`Rxml_set_live_reload`, `Rxml_set_auto_load`). Every Raven session turns both off right after `Rinit`: the tests from now on, the engine from M5.2.
  - What stays when they are off: OS file watches for windows and themes made from files (their events are dropped), `Rxml_check_files`, and the search for a relative reference that names no existing file.
  - Predictable loading: documents are given to Raven as text, and every file they reference exists where they say.
- **Tests** in `tests/unit/ui/`, part of `lpe_unit_tests`, the headless tier:
  - `raven_null_backend_test.cpp`: the pin, the XML window with injected click and text, failed starts, broken XML, repeated starts, no leaks;
  - `raven_configuration_test.cpp`: no native window system, live reload off, auto-loading off, HTTP compiled out.

### M5.2 — Raven in the engine loop and the editor shell

**Goal:** the engine hosts Raven on its window and OpenGL context; `lpe-editor` opens with the editor's empty shell.

**Scope:**
- **Backend** in `engine/src/render/opengl/`: the prototype's backend with the validation's fixes and its state comparison (the clear colour restored, the checked state). It is internal, behind functions the engine's UI object calls.
- **Host bridge** in `engine/src/platform/sdl3/`, with the validation's fixes:
  - held keys and buttons released on focus loss;
  - one close request for SDL's close request and quit;
  - no handle use after an accepted close.
  - It also passes `Rwindow_get_text_input_rect` to `SDL_SetTextInputArea`, for IME positioning (ADR 0019, *Acceptance* 3).
- **Main loop:**
  - raw SDL events go to the UI hook first, as `forwardEventToDebugUi` does for Dear ImGui;
  - Raven draws between `beginFrame` and `endFrame`, with its render thread disabled;
  - the display scale follows SDL, also when it changes while running.
- **The engine's public UI API** (`engine/ui/`), decided here (ADR): how an application turns Raven on (like `ApplicationDesc::debugUi`), what it gets from the engine (Raven's lifetime, its main window), and how the editor reaches Raven's C API without Raven types in other engine headers.
- **`editor/` with the target `lpe-editor`:**
  - an XML layout of the shell: toolbar, viewport, hierarchy, inspector and console areas, all empty;
  - the command-line options `--frames N`, `--screenshot FILE`, `--size WxH`.
- **Linux:**
  - **A Linux GPU-tier job in CI** (decision 3): GitHub Actions, Ubuntu 24.04, Xvfb, Mesa llvmpipe, Debug and Release. It runs the engine's GPU tier and the editor's runs. `docs/TESTING_AND_CI.md` describes it as software OpenGL, apart from hardware runs.
  - **Real graphics hardware:** not available (decision 2). Linux on hardware is recorded as not verified, the user's decision under ADR 0019's *Acceptance*. A problem found there later can reopen ADR 0019.

**Decisions to make (ADR "Raven in the engine"):** the public UI API; the event order between Raven, the debug UI and `InputState`; who owns the main Raven window; the frame order; what happens when Raven is off (island-preview, tests).

**Out of scope:** the viewport's content (M5.3), any panel's content, Dear ImGui in the editor.

**Acceptance criteria:**
- `lpe-editor` opens the shell and closes cleanly: 0 OpenGL errors, 0 state mismatches, Raven's memory back to 0.
- island-preview, its debug UI and its smoke runs are unchanged.
- No Raven header in a public engine header other than where the ADR allows it.

**Unit tests (headless):**
- The host bridge with the null backend: key mapping by meaning and by position, UTF-8 to code points, held input released on focus loss, one close request for close and quit, the scale passed on.
- The loop's event order with Raven on and off.

**GPU tier** (Windows locally on hardware; Linux in CI on llvmpipe):
- `lpe-editor --frames 60` without errors.
- The shell drawn: pixel checks of the toolbar and panel areas.
- Start and stop repeated: the engine's resources and Raven's memory stay level.

**Visual check:** `docs/screenshots/m5/001-editor-shell.png`.

**Decisions made in M5.2** (ADR 0027):
- **The public UI API.**
  - `ApplicationDesc::ui` (a `ui::UiDesc` with the main window's XML layout as text) turns Raven on.
  - `AppContext::ui()` gives `mainWindow()` as `ui::RavenId` (a `std::uint32_t` equal to Raven's `Rid`) and `scale()`.
  - `RunSummary::ui` reports the UI frames, OpenGL errors, state mismatches, Raven's errors and Raven's allocations after `Rshutdown`.
  - `engine/include/engine/ui/Ui.hpp` names no Raven, SDL or OpenGL type. The editor includes Raven's headers and calls its C API itself.
- **Ownership.**
  - One engine object (`engine/src/ui/RavenUi`) owns Raven's state. It is created after the renderer and destroyed before it.
  - It registers the host platform and the OpenGL backend, calls `Rinit_ex` (custom backend, render thread disabled), turns live reload and auto-loading off at once, and builds the main window from the layout with continuous redraw.
  - `Rshutdown` comes after `Application::stop`. A failed step undoes the earlier ones, and a second UI is refused.
  - The engine owns the main Raven window; the loop ends when Raven closes it.
- **Event order** in `Platform::pollEvents`:
  1. Dear ImGui sees every event.
  2. The Raven host takes keyboard, text, mouse and wheel events and close requests (one per close and quit). It sees focus, size and scale events and passes them on.
  3. `InputState` and the quit request get the rest. Focus loss always resets `InputState`.

  With Raven on, `InputState` sees no keyboard or mouse input: in the editor, input for the 3D view comes through the viewport widget (M5.3).
- **Frame order.** `beginFrame`, `Application::render`, Raven's frame (inside an OpenGL state comparison), `endFrame`, screenshot, swap. Raven's frame covers the window, and its window is redrawn every frame.
- **Without Raven.** `debugUi` and `ui` together are refused (one UI stack per application). island-preview and the debug UI are unchanged and never start Raven.
- **The backend and the host** come from the prototype with the validation's four fixes.
  - The shaders are embedded from `third_party/raven` at build time (`cmake/EmbedFiles.cmake`).
  - The host passes Raven's caret to `SDL_SetTextInputArea`, and turns SDL text input on only while a Raven text field has the keyboard.
- **The editor.**
  - `editor/` has the library `editor` and the program `lpe-editor`, with options `--frames`, `--screenshot` and `--size`.
  - The empty shell is `editor/layouts/shell.xml` (toolbar; viewport, hierarchy, inspector and console in a DockArea with fractional sizes), embedded at build time.
  - Exit codes: 2 for usage, 3 for OpenGL errors, 4 for a failed screenshot, 5 for UI problems.
- **Linux GPU tier.** It is part of the existing Linux CI jobs: Xvfb + Mesa llvmpipe with `LIBGL_ALWAYS_SOFTWARE=1`, checked to be llvmpipe and reported as software OpenGL. It also runs `lpe-editor` at 100 % and at 150 % (`Xft.dpi: 144`) and keeps the logs and screenshots.
- **Not verified in M5.2.**
  - Linux on real graphics hardware (decision 2).
  - A real 150 % display or a live scale change on Windows. The validation checked them before M5; here they are covered by the headless layout tests, the scale events and the Linux run at `Xft.dpi` 144.
  - IME composition with a real IME.

### M5.3 — The generated world in the viewport

**Goal:** the editor's viewport shows the generated archipelago under the sky, with a free-fly camera.

**Scope:**
- `WorldScene` and `Sky` move from `sandbox/island-preview/` to `engine/world/`. island-preview uses them from there, and its output and smoke runs stay the same.
- The viewport is Raven's draw callback. It draws the world with the default world settings; projects come in M5.4 and M5.11.
- The editor camera: right button to look, WASD, Q and E when the viewport has the keyboard, the wheel for speed. It follows the viewport's size.

**Out of scope:** scene entities (M5.6), picking (M5.9), the WorldGen panel (M5.10).

**Acceptance criteria:** the archipelago draws inside the viewport only, the rest of the shell around it; resizing follows; 0 OpenGL errors and state mismatches.

**Unit tests:**
- The moved code's tests run unchanged.
- The viewport camera's input mapping, headless with the null backend: keys only while the viewport is focused, no camera movement while a text field has the keyboard.

**GPU tier:** sky and island pixels inside the viewport's rectangle and not outside it; island-preview's smoke runs unchanged.

**Visual check:** `002-world-viewport.png`.

**Decisions made in M5.3** (ADR 0027, Amendment 1):
- **The world module** `engine/world/` (namespace `engine::world`):
  - `WorldScene` and `Sky` moved from `sandbox/island-preview/` with only their namespace and comments changed (Git follows the files). `skyView()` is island-preview's `rotationOnly`: the camera's rotation without its position.
  - `WorldView.hpp`: `archipelagoSunlight()`, `cameraLookingAt()` and `overviewCamera()` moved from island-preview's `main.cpp` as well, because the editor shows the same world in the same light from the same overview. island-preview's close view of one island and its test scene stay in the sandbox.
  - It uses render, worldgen, camera and geometry; no Raven, SDL, OpenGL or Dear ImGui (the public header check covers it).
  - **island-preview unchanged:** five runs (default, `--island 0`, `--test-scene`, `--panel`, `--seed 42`) before and after the move differ in their screenshots only as much as two runs of the same build do, since the clouds drift and the test scene's objects spin with measured time and the overlay shows frame times. The archipelago log lines are equal. Its smoke runs pass unchanged.
- **The viewport** (`editor/src/viewport/WorldViewport`):
  - At start it generates the archipelago of the default `WorldGenSettings`, uploads it and the sky dome, and puts the camera on the overview. It is the last step of `EditorShell::start`, so nothing can fail after it; it is released in `Application::stop`, before Raven shuts down.
  - It draws from the viewport's draw callback as island-preview draws: the sky without depth, the islands and clouds lit by the archipelago's sun, the water blended back to front. The projection uses the widget's aspect in every frame, so the world follows the widget's size.
- **The editor camera** (`editor/src/viewport/ViewportInput`), on the viewport widget's events:
  - W/A/S/D, E up, Q down, Shift faster, the wheel over the viewport for the speed: the engine's free-fly controller at 30 m/s, as in island-preview. The arrow keys, Space and Ctrl are not used: Ctrl stays free for the editor's shortcuts.
  - Keys reach the camera only while the viewport has Raven's keyboard focus. A press of any mouse button in the viewport gives it the focus, and it has the focus at start, so the camera flies at once. When it loses the focus, held keys are forgotten and looking ends.
  - Hold the right button to look around. The press is passed on to the viewport, which keeps the pointer while it is held, also over other areas and outside the window. The movement comes from Raven's event deltas, in pixels; there is no relative mouse mode, so looking stops at the edge of the screen.
- **The `editor` library links `raven` publicly**, since its headers name Raven's types (widget IDs, events).
- **The editor logs its camera when it stops** (position, yaw, pitch, speed), for checks with real input.
- **Checked on Windows with real input** (SendInput through the operating system, the Debug and the Release build, one run per case, closed with `WM_CLOSE`; the driver script is not committed, like M5.2's close check):
  - W held 1 s: the camera moves 30 m along its view, its direction unchanged;
  - a right-button drag of 200 pixels: yaw changes by 0.52 rad, the position does not;
  - two wheel notches: the speed goes from 30 to 46.9 m/s;
  - a click in the hierarchy, then W: the camera stays; a click back in the viewport, then W: it moves;
  - W held, then another window in front and W released there: the camera stops when the editor loses the focus.
  - Every run: exit code 0, 0 OpenGL errors, 0 state mismatches, 0 Raven errors, 0 Raven blocks after shutdown.
  - A driver that clicks right after moving the pointer with `SetCursorPos` gets the click at the old position from SDL; the driver waits for the move, as a real mouse gives one.
- **Not verified in M5.3:** Linux on real graphics hardware (decision 2); a real 150 % display on Windows (Linux CI runs the editor at 150 % on software OpenGL); real input on Linux.

### M5.4 — Project folder and project document

**Goal:** a project folder holds everything the editor opens and saves.

**Scope:**
- **The folder:** the project document, the asset manifest (M4.3), `scenes/`, `worlds/`, and the asset folders the manifest points to.
- **A project document** (ADR 0004 envelope, its own `format` id, version 1): the project's name, the manifest's path, the start scene's path, and the world settings file's path. All paths are project-relative under the M4.3 path rules.
- **New:**
  - folders created;
  - an empty manifest;
  - an empty scene with a new scene ID;
  - default world settings.
- **Open:** the document, the manifest into a registry, the scene checked against the registry, the world settings. All or nothing; errors name the file.
- **Save:** every part, consistently as a whole (see below).
- **Where the code lives:** `engine/project/` (decision 5).
  - The project API and model depend on no editor, Raven, SDL3 or OpenGL code; a header check test enforces it.
  - A runtime application can open the same format later.
- **Saving several files consistently** (addition 2):
  - Writing each file atomically (`core::writeFileAtomically`) is not the same as saving the project atomically. A failure after some files leaves a mix of old and new data.
  - The ADR decides how a save of several files stays consistent, and what opening does with an interrupted save. For example:
    - all new files written under temporary names first, then put in place in a fixed order, with a generation marker the open checks;
    - or a project document written last that names the generation of its files.

**Decisions to make (ADR "Project document"):** the format id and file name, required and optional fields, what happens with a missing world settings file, the folder names, whether the scene list holds more than the start scene, and the consistency of a save of several files (above).

**Out of scope:** UI (M5.11), several open scenes, asset import.

**Acceptance criteria:**
- A project created, saved, moved to another folder and opened again is identical.
- A broken part leaves nothing half-opened.
- A save that fails at any point leaves a project that opens either as the previous save or as the new one, never as a mixture.

**Unit tests:**
- Exact layout; round trip.
- Missing, unknown and wrongly typed fields; newer version.
- Paths outside the project, or absolute.
- Each part missing or broken.
- **Write failures injected at every file and every stage of a save:** afterwards the project opens as entirely the old or entirely the new version, never a mix, and leftover temporary files are removed or ignored.
- A moved folder.

**Visual check:** none.

**Decisions made in M5.4** (ADR 0028; ADR 0004 Amendment 3):
- **The folder and the document.**
  - A project is opened by its folder. The project document has the fixed name `project.json` (format `lpe.project`, version 1).
  - Its fields, all required: `name` (the rules of scene names), `assetManifest`, `startScene`, `worldSettings` (project-relative paths under ADR 0022's rules).
  - The paths must differ (also in letter case only), must not lie inside one another, and must not be `project.json` or the journal's `project.saving.json`. No asset of the manifest may have one of them.
  - A new project's defaults: `assets.json`, `scenes/main.scene.json` (a scene "Main"), `worlds/world.json` (the default settings, equal to `assets/worlds/default.json`). Asset folders are not created.
- **Only the start scene** in version 1. A list of scenes comes with a later version and its migration.
- **A missing world settings file is an error**, like any part: no silent defaults that the next save would write over the project's world.
- **A new project never overwrites a file:** an existing project, save journal or default file is refused, naming it.
- **Open:**
  - all or nothing, and read-only;
  - the order: journal, document, manifest, the scene checked against the manifest's registry, world settings;
  - every error starts with the part's project-relative path, and warnings carry it too.
- **Save:** consistent as a whole through a save journal; the details are in ADR 0028, point 5.
  - The save first checks everything and turns every part into text, so it never writes a project that would not open.
  - It finishes what an earlier save left.
  - It writes every part to `<file>.saving-<id>`.
  - The commit is the rename of the journal into place.
  - Then the temporary files are renamed over the parts, and the journal is removed.
  - An interrupted save after its commit opens as that save: a part whose temporary file is still there is read from it. Without a journal, temporary files are ignored and removed by the next save.
- **Code:**
  - The public API is `engine/include/engine/project/` (`ProjectDocument.hpp`, `Project.hpp`).
  - The implementation is `engine/src/project/`. Every file operation goes through the internal `ProjectFiles`, which the tests replace to inject failures.
  - The module uses core, assets, scene and worldgen only; a test checks its includes.
- **Tests:** the failure injection makes every changing file operation of a save fail in turn: alone, and as a crash with a half-written file.
  - The save from version 1 to 2 has 15 such operations.
  - The save after a crash just past the commit is covered the same way.
  - Breaking open's use of the journal on purpose made the test fail at once: the project opened as the wrong save, or not at all (a new manifest next to an old scene).
- **Not covered:** a power loss (no `fsync`, as in ADR 0023); two programs saving the same project at once.

### M5.5 — Editor document, selection and undoable commands

**Goal:** every change to the document is a command that can be undone and redone exactly.

**Scope:**
- **The document:** the open project, its scene, its world settings and its asset registry, with a dirty state against the last save.
- **The selection:** entity IDs, kept valid when entities are deleted or come back through undo.
- **Commands:**
  - create an entity, also with given components;
  - delete an entity (undo restores its ID, its components, its parent link and its children's links);
  - rename;
  - add, replace or remove a component;
  - set or clear a parent;
  - set the world settings.
- **The command stack:** undo, redo, a size limit, merging of continuous edits (a dragged value is one undo step), a clean marker for "saved". A failed command changes nothing and reports why.

**Decisions to make (ADR "Editor document and commands"):** the command interface, how a command stores what it changed, merge rules, the limit, what undo does after a save.

**Out of scope:** UI, project files (M5.4 provides them), several scenes.

**Acceptance criteria:**
- After any sequence of commands and the same number of undos, the scene document is byte for byte the original.
- After the redos it is byte for byte the edited one.

**Unit tests:**
- Each command: apply, undo, redo.
- Deleting a parent with children and undoing it.
- IDs never change through undo.
- Merging, the limit, the dirty state after save, undo and redo.
- Failures that change nothing.
- The selection across deletes and undos.
- 10 000 commands.

**Visual check:** none.

### M5.6 — Scene entities in the viewport

**Goal:** the scene's entities with a static mesh appear in the viewport where their transforms put them.

**Scope:**
- An `engine/world/` view of a scene for the renderer:
  - each mesh asset resolved through the registry, read by the OBJ loader and uploaded once (a cache by asset ID, released with the document);
  - entities drawn with their own transform (no transform inheritance, as in M4);
  - every material reference drawn with the default material (material files come in M6);
  - a missing or broken mesh reported once and skipped.
- The scene's light: the first directional light component, or the world's sun when there is none. Camera components are not drawn.
- The view follows the document's changes; it does not rebuild every frame.

**Decisions to make:** how changes reach the view (the document's change notifications or a version counter); the placeholder for missing meshes, if any.

**Out of scope:** picking (M5.9), materials, prefab instances shown differently, transform inheritance.

**Acceptance criteria:**
- A scene with mesh entities draws at the right places with the world around it.
- Renderer resources stay level while entities are added and removed.

**Unit tests:**
- Which entities are drawn, with which model matrices.
- Missing, unregistered and wrongly typed assets.
- The cache across changes and a new document.

**GPU tier:**
- A test scene with the lighthouse mesh: pixel checks at the expected places.
- Resource counts after edits and undo.

**Visual check:** `003-scene-entities.png`.

### M5.7 — Hierarchy and console panels

**Goal:** the scene's entities as a tree that creates, deletes, renames and reparents through commands, and a console with the engine's log.

**Scope:**
- **Hierarchy** (Raven TreeView):
  - entities in creation order, children under their parent (`ParentComponent`), shown by name or by ID;
  - create, delete (key and button), rename in place, drag to reparent (the scene's cycle rules give the message);
  - the selection shared with the document.
- **Console:**
  - a log listener added to `engine/core/Log.hpp`, so the engine's log reaches the editor as well as the terminal; thread rules stated;
  - the console lists lines with their level and filters them;
  - document warnings and errors (e.g. from opening) appear there too.

**Decisions to make:** the log listener API (a small public core addition, ADR if it is more than a callback), how the tree keeps its items in step with the document.

**Out of scope:** multi-selection editing, prefab instances shown as groups, search.

**Acceptance criteria:** every hierarchy action is one undo step; the tree always matches the scene, also after undo and redo; log lines appear within a frame.

**Unit tests:**
- Headless Raven tests of the hierarchy with the null backend: the tree after create, delete, rename, reparent, undo and redo, through injected clicks and keys.
- The log listener: order, levels, removal while logging.

**GPU tier:** the editor with a test project shows the tree.

**Visual check:** `004-hierarchy-console.png`.

### M5.8 — Inspector

**Goal:** the selected entity's components shown and edited.

**Scope:**
- A section per component:
  - name;
  - transform: position, rotation as Euler angles in degrees (the conversion to and from the quaternion is decided here, together with how a stored quaternion is shown), scale;
  - camera;
  - directional light;
  - static mesh: mesh and material chosen from the registry's assets of that type, material "default";
  - script: asset and enabled.
- Add and remove components.
- Edits are commands; a continuous edit merges into one step. A value the component's `validate` refuses is not applied, and its message is shown at the field.

**Decisions to make:** the Euler convention and the rounding shown; which Raven widgets (spin boxes, text inputs, combo boxes); how a field shows a refused value.

**Out of scope:** several selected entities at once, prefab overrides, asset import.

**Acceptance criteria:** each field reaches the scene through one command; a refused value leaves the scene unchanged; undo restores the exact stored value.

**Unit tests:**
- Euler and quaternion conversion: round trips, gimbal lock, the values M4 stores.
- Headless Raven tests of every section with typed values, refused values and undo.

**GPU tier:** the inspector drawn for a test entity.

**Visual check:** `005-inspector.png`.

### M5.9 — Picking and selection in the viewport

**Goal:** a click in the viewport selects the entity under the pointer, and the selection is visible.

**Scope:**
- **Picking:** a ray from the viewport camera through the clicked pixel, tested against the mesh entities first by bounds and then by triangles (CPU data kept by the scene view, M5.6). The nearest hit wins; a click on nothing clears the selection. The generated islands are not entities and are not picked.
- **Highlight:** the selection is shown in the viewport (the way is decided here, with the existing renderer API) and kept in step with the hierarchy and the inspector.

**Out of scope:** gizmos (decision 4), box selection, picking islands.

**Acceptance criteria:** the click picks what is drawn under the pointer, at 100 % and at a scaled display; the highlight follows selection changes from every panel.

**Unit tests:** rays and hits — golden cases, rotated and scaled transforms, near and far, entities behind each other, missing meshes, the viewport's pixel-to-ray mapping at several scales.

**GPU tier:** click, highlight drawn, the selection set.

**Visual check:** `006-picking.png`.

### M5.10 — WorldGen panel in Raven

**Goal:** the world settings edited in the editor, and the archipelago regenerated in the viewport.

**Scope:**
- The panel's model (`WorldGenPanel` of M3.12: groups, clamping, revert, defaults, regeneration of valid settings) moves out of `sandbox/island-preview/` to shared code. island-preview's Dear ImGui panel keeps working on it unchanged.
- A Raven panel in the editor on the same model.
- Applying settings is a command (undo and redo regenerate); the settings are saved with the project.

**Out of scope:** changes to WorldGen, a preview of settings before applying, async generation.

**Acceptance criteria:**
- Every field from `settingsFields` can be edited.
- Regeneration happens only for valid settings.
- Undo brings the previous world back.
- island-preview's panel and its tests are unchanged.

**Unit tests:** the moved model's tests; headless Raven panel tests; undo of a regeneration.

**GPU tier:** regeneration keeps renderer resources level (as in M3.12).

**Visual check:** `007-worldgen-panel.png`.

### M5.11 — Project workflow: new, open, save, close

**Goal:** the editor's file workflow, without losing unsaved changes silently.

**Scope:**
- New project, open project, save (Ctrl+S), close.
- **Raven's folder dialog, checked on Windows and on Linux** (addition 1), because the Raven validation did not test it:
  - opening the dialog;
  - navigation;
  - choosing a folder;
  - cancel.
  - If it fails on a platform, the step uses a fallback and records it. Candidates: SDL3's native folder dialog (`SDL_ShowOpenFolderDialog`), or a path field with the project's checks.
- A prompt about unsaved changes before close, open or new.
- The window title shows the project and the unsaved state.
- `lpe-editor <project folder>` opens a project at start.

**Out of scope:** recent projects, autosave, several windows.

**Acceptance criteria:**
- Save, close and reopen gives the same documents byte for byte.
- No path loses unsaved changes without a prompt.

**Unit tests:** the workflow's states (clean, unsaved, prompts, cancel, failed save) headless; the folder dialog's flow (navigate, choose, cancel) with the null backend.

**GPU tier / integration:**
- Open the committed test project, edit through injected input, save, close, reopen, compare.
- The folder dialog opened, navigated, used to choose a folder and cancelled: on Windows by real input on hardware; on Linux under Xvfb in CI (software OpenGL).

**Visual check:** `008-project-workflow.png`.

### M5.12 — Integration, final verification and pull request

**Goal:** close M5 against its exit criterion. No new features.

**Scope:**
- **An end-to-end run** of the exit criterion through injected input in the GPU tier: create entities, edit transforms and components, regenerate the world, save, close, reopen, and compare.
- **The full checks** in Debug and Release:
  - both tiers, every wrapper, a fresh clone;
  - CI green;
  - Linux: the CI runs under Xvfb (software OpenGL) reported as such; Linux on real graphics hardware recorded as not verified (decisions 2 and 3).
- **A manual check by the user:** fly, select, edit, undo, save and reopen.
- **Documentation:** `docs/ENGINE_PLAN.md`, `README.md`, `docs/PROJECT_STRUCTURE.md`, `docs/TECH_STACK.md`, `docs/TESTING_AND_CI.md`, the ADRs, `docs/screenshots/m5/`.
- **The pull request** `M5: Minimal editor` into `main`, merged with a merge commit after the user accepts M5.

**Done when:** the checks pass, the pull request is open and the user accepts M5.

**Visual check:** the screenshots of M5 complete.

## ADRs expected in M5

| Topic | Step | Notes |
|---|---|---|
| Raven packaging | M5.1 | pruned vendored source (decision 1): what is kept, options, notices, the update procedure |
| Raven in the engine | M5.2 | public UI API, event order, frame order, ownership |
| Project document | M5.4 | format, folder layout, paths, all-or-nothing open, a consistent save of several files (ADR 0028) |
| Editor document and commands | M5.5 | command model, undo, merging, dirty state |
| Amendments | as needed | ADR 0019 (integration details), ADR 0018 if the debug UI's hook changes |

## Out of scope for M5

These stay out:

- **Editing tools:** transform gizmos (decision 4: a later milestone, after the minimal editor; transforms are edited in the inspector), multi-selection editing, prefab authoring and overrides.
- **Running the game:** play mode and running Lua scripts inside the editor.
- **Content (M6):** the asset browser and asset import, material files and editing.
- **Scene model:** transform inheritance; several scenes open at once; dock layout saved per project.
- **Other UI:** replacing Dear ImGui in island-preview or the engine's debug UI.
- **The usual exclusions** (AGENTS.md §11): Vulkan, terrain sculpting, characters, NPCs, physics, audio, animation, networking.

## Definition of done for M5

1. Raven is a pinned dependency built on Windows and Linux.
2. The engine hosts Raven with the validated fixes; island-preview and the debug UI are unchanged.
3. `lpe-editor` opens, creates and saves projects; a failed save never leaves a mixed project; the folder dialog works on Windows and Linux, or the recorded fallback does.
4. The viewport shows the world and the scene's entities; clicks select entities.
5. Hierarchy, inspector, WorldGen panel and console work through commands.
6. Undo and redo cover every document change.
7. Save, close and reopen restore the scene and the world exactly.
8. All headless and GPU tests pass in Debug and Release; CI is green on Windows and Linux.
9. Linux runs in CI under Xvfb with software OpenGL, reported as such; Linux on real graphics hardware is recorded as not verified (decisions 2 and 3).
10. The documentation and screenshots are updated.
11. The pull request is accepted by the user and merged into `main`.

## Findings while preparing the plan (2026-11-15)

- **Stale statuses:** `docs/ENGINE_PLAN.md` and `README.md` still showed M4 as waiting for acceptance; this commit records it as accepted and merged through pull request #5 (`1425c2c`).
- **Editor UI wording:** `docs/ENGINE_PLAN.md` §9 began "Start with Dear ImGui" for the editor; ADR 0019 (accepted) changes this, and §9 is updated here.
- **Raven docs not on `main`:** ADR 0019's acceptance, the final validation report and its screenshots were only on `experiment/raven-final-validation`; they come to `main` with this branch. The prototype's code does not.
- **No `editor/` yet:** `docs/PROJECT_STRUCTURE.md` proposes one with `src/ui/imgui/`; the editor's UI folder becomes Raven's.
- **Code the editor needs lives in the sandbox:** `WorldScene`, `Sky` and the WorldGen panel model are in `sandbox/island-preview/`; M5.3 and M5.10 move them to the engine, where island-preview keeps using them.
- **No log listener:** `engine/core/Log.hpp` writes to the terminal only, so the console needs one (M5.7).
- **No render targets:** the renderer draws only to the window; the viewport uses Raven's draw callback (ADR 0019), so none is added.
- **No transform inheritance:** parent links carry none (M4), so children do not follow their parents in the viewport; the hierarchy shows the links only.
- **No material files yet:** they come in M6 (ADR 0010), so the viewport draws every material reference with the default material.
- **Raven's own facts:**
  - it has a null backend and a null platform for tests (`R_BACKEND_NULL`), which makes headless panel tests possible;
  - its pinned source is about 37 MB with tools, docs, examples and bindings, about 6.6 MB pruned to what the build needs.

## Decisions on the open questions (approved 2026-11-15)

The user approved the plan with the recommended answers, the requirements below, and two additions.

1. **Raven packaging — pruned vendored source in `third_party/raven/`.**
   - Fixed version 0.2.3 at commit `7ab270df3b618de3d11af6033912374b6cd621b7`.
   - No downloads during configure or build.
   - `LICENSE`, `NOTICE` and the licences of all bundled third-party components are kept.
   - A Raven update is a separate, reviewable change.
   - The pruned copy's contents and the update procedure are documented in detail (M5.1).
2. **Linux on real graphics hardware — not available.** It stays recorded as not verified in M5.
3. **The GPU tier on Linux in CI — yes, from M5.2.**
   - GitHub Actions, Ubuntu 24.04, Xvfb, Mesa llvmpipe.
   - Documents and reports keep software OpenGL in CI apart from checks on real graphics hardware; CI does not replace a hardware test.
4. **Transform gizmos — out of scope for M5.** Transforms are edited in the inspector. Gizmos can come in a later milestone, after the minimal editor.
5. **The project document — in `engine/project/`.**
   - The project API and model stay independent of the editor, Raven, SDL3 and OpenGL.
   - A runtime application will later open the same project format.
6. **No Dear ImGui in the editor.**
   - `lpe-editor` uses Raven only; Raven and Dear ImGui are not combined in the editor's interface.
   - Dear ImGui stays for the engine's debug UI, island-preview's WorldGen panel, and as the fallback should the project ever have to leave Raven.

**Additions:**

1. **M5.11 checks Raven's folder dialog** on Windows and Linux: opening, navigation, choosing a folder, cancel. These were not tested in the Raven validation. If it has problems, the step uses an alternative way to choose a folder.
2. **M5.4 decides in its ADR how a save of several project files stays consistent.** Writing each file atomically is not the same as saving the whole project atomically. Tests inject write failures, so that a failed save never leaves the project as a mixture of old and new data.

## Risks

- **Raven 0.x API changes:** the pin and the vendored copy keep it stable; an update is a reviewed step of its own, with the backend's shader adaptation failing loudly when Raven changes.
- **Raven's process-wide state:** one `Rinit` per process limits tests to one Raven instance at a time; the test layout keeps them in one executable run in sequence.
- **Headless panel tests vs real drawing:** the null backend proves logic and layout, not pixels; the GPU tier and screenshots cover drawing.
- **Linux on hardware stays unverified in M5** (decision 2). The CI runs on software OpenGL; a driver problem found on hardware later can reopen ADR 0019.
- **Scope pressure:** gizmos, play mode, an asset browser and materials are tempting with Raven's widgets; they stay out unless the plan is changed first.
- **Raven's folder dialog** was not covered by the Raven validation; M5.11 checks it on both platforms and has a fallback.
- **Saving a project of several files** can leave a mixture after a failure; M5.4's ADR and failure tests prevent it.
- **Euler angles:** converting stored quaternions to editable angles can surprise (gimbal lock, angle ranges); M5.8 decides and tests the convention.
