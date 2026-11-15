# Raven final validation (before M5)

- **Status:** done (2026-11-15). **Decision: APPROVED** (see *Final decision*), accepted by the user on 2026-11-15; ADR 0019 is accepted.
- **Branch:** `experiment/raven-final-validation`, created from `main` at the M4 merge `1425c2c`, with the prototype of `experiment/raven-ui-evaluation` merged in (`7166a16`). Nothing here is merged into `main`; M5 has not started.
- **Follows:** `docs/experiments/raven-ui-evaluation.md` (conditional pass on Windows, 2026-11-14) and ADR 0019 (proposed). The user accepted the result and ADR 0019 on 2026-11-15.
- **Prototype and tools:** `sandbox/raven-evaluation/` (how to build: its `README.md`), `sandbox/raven-evaluation/input_driver.py` (real OS input), `.github/workflows/raven-validation.yml` (Linux). Screenshots: `docs/experiments/raven-final-validation/`.

## 1. Goal

The evaluation left these checks open before Raven can be the main UI of the M5 editor (ADR 0019, *Preliminary acceptance* and condition 6):

- Linux build and run;
- a display really scaled above 100 %;
- the clipboard and the text widgets the editor needs;
- focus loss and focus restore, without stuck keys;
- the limits of the host platform;
- whether the project can maintain its own OpenGL backend and SDL3 bridge.

This validation also checks that Raven and Dear ImGui (the debug UI, ADR 0018) can live in one application, and that nothing regresses in the engine. The question it answers: can Raven be the main UI of the editor from M5 on, with Dear ImGui kept for debug windows and as a fallback? Dear ImGui is not removed or deprecated.

The engine is not changed: everything is in the opt-in prototype (`LPE_BUILD_RAVEN_EVALUATION`, default OFF). Scene, core, assets and scripting do not depend on Raven, OpenGL or SDL.

## 2. Environment

**Windows**

- Windows 11 IoT Enterprise LTSC 2024 (10.0.26100).
- MSVC 19.51.36260 (Visual Studio 2026), CMake 4.5.0-rc3.
- NVIDIA GeForce RTX 4060, driver 610.88 (32.0.16.1088).
- One monitor, 3440×1440. Display scale 100 % (96 DPI). The user also set 150 % (144 DPI) in *Settings → System → Display → Scale*, and changed it while the program ran: 100 → 150 % and 150 → 100 %.

**Linux**

- GitHub Actions `ubuntu-24.04` runner; GCC 13.3.0, CMake from the runner, Ninja.
- X server: Xvfb 1920×1080×24, without a window manager.
- OpenGL: Mesa 25.2.8, `llvmpipe` (LLVM 20.1.2), software rendering, OpenGL 4.5 Core. **No GPU.**
- Input: XTEST through `xdotool`; keyboard layouts with `setxkbmap`; the clipboard through `xclip`.
- 150 %: `Xft.dpi: 144` in the X resources (`xrdb -nocpp -merge`, Xvfb with `-noreset`), as a Linux desktop sets it; SDL reports a display scale of 1.5 from it.

**Common**

- Raven 0.2.3 at commit `7ab270df3b618de3d11af6033912374b6cd621b7`, the evaluation's pin.
- SDL 3.4.16, Dear ImGui 1.92.9b, the engine as merged after M4.
- Both builds use the project's warnings as errors (`LPE_WARNINGS_AS_ERRORS=ON`): MSVC `/W4 /WX`, GCC `-Wall -Wextra -Wpedantic -Wshadow -Wconversion … -Werror`.

## 3. Existing Raven architecture (the prototype)

```text
SDL3 window + OpenGL 4.5 context (one window, one loop, main thread)
 ├─ SDL events ─▶ [Dear ImGui first, --with-imgui] ─▶ SdlRavenHost ─▶ Rhost_inject_*
 ├─ engine::render::Renderer      the archipelago, drawn in Raven's viewport callback
 ├─ Raven (host platform, R_BACKEND_CUSTOM, render thread disabled)
 │   └─ GlRavenBackend            RBackendVTable on the engine's context
 └─ Dear ImGui (debug UI)         drawn after Raven's frame, before the swap
```

- **Host bridge** (`SdlRavenHost`):
  - It turns SDL events into `Rhost_inject_*`: mouse, wheel, keys (by meaning in Latin layouts, by position otherwise), UTF-8 text as code points, focus, mouse leave, size and scale, close.
  - It serves Raven's cursor, clipboard and wake-up requests through SDL.
  - SDL window coordinates (points) times SDL's pixel density give pixels. Raven gets the pixel size and `SDL_GetWindowDisplayScale`, and its logical units are pixels / scale.
- **OpenGL backend** (`GlRavenBackend`):
  - Raven's Vulkan shaders, adapted at load time.
  - DSA textures and instanced draws.
  - `RenderFrame` saves the OpenGL state, draws, and restores it; a state comparison counts every difference.
  - Viewport callbacks get the engine's own state, clipped to the widget.
- **Event bridge:**
  - One poll loop. Each event goes to Dear ImGui (when on), then to the host, unless Dear ImGui wants the mouse or the keyboard.
  - The camera hears the viewport's events through `Rwidget_set_event_callback`.
- **Viewport:** the draw callback is the main path; a CPU read-back texture is kept for comparison.
- **Input:** text needs `SDL_StartTextInput`. Raven's single-line editor (`textedit.c`) handles caret, selection, word moves and Ctrl+A/C/X/V through the host's clipboard.

## 4. Test matrix

How each was checked:

- **Self-test:** 23 steps of input pushed into SDL's queue (`--self-test`).
- **Real input:** `input_driver.py` with `--input-check`:
  - Windows: SendInput, SetCursorPos, the Win32 clipboard, SetForegroundWindow.
  - X11: xdotool, setxkbmap, xclip.
  - 43 checks; 53 with `--with-imgui`; 47 with `--scale-change`.
- **Stress:** Raven started and stopped N times (`--stress N`).

| Check | Windows | Linux | Result |
|---|---|---|---|
| Build Debug | PASS (MSVC, `/W4 /WX`, 0 warnings in project code) | PASS (GCC 13.3, `-Werror`, 0 warnings) | PASS |
| Build Release | PASS | PASS (GCC 13.3, `-Werror`, 0 warnings) | PASS |
| Runtime | PASS (RTX 4060) | PASS on Xvfb + llvmpipe; **GPU runtime NOT VERIFIED** | PASS (Linux without GPU) |
| OpenGL backend | PASS: 0 OpenGL errors, 0 state mismatches (extended comparison, also around the whole frame) | PASS (llvmpipe) | PASS after fix 2 |
| SDL input | PASS: clicks exact to the pixel, wheel, drag, keys, events not handled twice | PASS | PASS |
| Text input | PASS: ASCII by key presses, editing keys, Shift selection, Ctrl+A | PASS (XTEST key presses, US layout) | PASS |
| Cyrillic UTF-8 | PASS: "Привет, мир!" with the Russian layout; "Тестовый текст: 123." as Unicode packets | PASS (both texts typed with the Russian XKB layout) | PASS |
| Clipboard | PASS: Ctrl+C/X/V with the system clipboard, BMP and non-BMP, several lines, empty, after a window switch, one read/write per shortcut | PASS (xclip) | PASS |
| Focus loss | PASS: W, Shift, Ctrl, Alt and the right button held across a real focus loss; nothing stuck | PASS | PASS after fix 1 |
| Focus restore | PASS: the TextInput and the viewport keep Raven's keyboard; text input stays active | PASS | PASS |
| HiDPI >100 % | PASS at 150 % set in Windows; live change 100 ↔ 150 % | PASS at 150 % through `Xft.dpi` (Xvfb, no physical display) | PASS (Linux on a virtual X server) |
| Mouse coordinates | PASS at 100 % and 150 % (≤ 0.5 px, rounding) | PASS | PASS |
| Resize | PASS: window, layout and viewport follow; clicks right after | PASS | PASS |
| Dear ImGui coexistence | PASS: own clicks and typing, none reaching Raven; Raven unaffected; 0 state mismatches around Dear ImGui's renderer | PASS | PASS |
| Shutdown | PASS: WM_CLOSE through Raven's close callback, 0 Raven blocks after `Rshutdown`, exit code 0 | PASS (SIGTERM) | PASS after fixes 3 and 4 |

### Runs

**Windows Debug and Release**, each with and without Dear ImGui, at 100 % and 150 %:

- **Self-test:** 23/23 in every run.
- **Real input** (after the fixes): 43/43 without Dear ImGui (Debug twice, Release once); 53/53 with it (Debug and Release, at 100 % and 150 %); 54/54 with `--expect-scale 1.5`.
- **Live scale change:** 47/47.
- **Stress:** 10/10 at 100 % and 5/5 at 150 % (Release).

**Linux Debug and Release** (Xvfb, llvmpipe):

- **Self-test:** 23/23, with and without Dear ImGui.
- **Stress:** 3/3.
- **Real input:** 54/54 (with Dear ImGui and the scale check) at 100 % and at 150 %, in Debug and in Release.

**One-off:** in the first Windows run, one click landed 26 px right of the target. y was exact and the widget was still hit. It did not happen again in any of the eleven later runs, at 100 % or 150 %; most likely the mouse was moved during that run.

## 5. Problems found

**1. Keys held across a focus loss** (host bridge)

- **Problem:** Raven's `Rhost_inject_focus(false)` ends captures, but it sends the focused widget no key-up or button-up. The OS delivers the real releases to whichever window has the focus then. The prototype used to clear its camera keys in the application loop.
- **Root cause:** Raven's host platform does not release held input on focus loss. On a real focus loss SDL3 itself sends key-ups for held keys (W, Shift, Ctrl and Alt: one each), but **not** a button-up for a held mouse button.
- **Fix** (`0c0d033`): `SdlRavenHost` tracks the keys and buttons it gave Raven. On `SDL_EVENT_WINDOW_FOCUS_LOST` it injects their releases (modifiers last) before the focus change. Releases SDL already sent empty the lists first, so nothing is released twice. The application-level reset is gone.
- **Verification:**
  - Self-test: "the host released 1 held key".
  - Real focus loss on Windows and Linux: SDL released the four keys, the host released the right button. Afterwards no viewport key, no Raven key or button and no SDL key or button state is held, and the camera stays still.

**2. Clear colour left changed** (OpenGL backend)

- **Problem:** `RenderFrame` cleared with Raven's colour and did not restore `glClearColor`.
- **Root cause:** the clear colour was not in the saved state, so the backend's own comparison could not see it. Nothing looked wrong only because the engine sets its clear colour in every `beginFrame`.
- **Fix** (`fee45d1`):
  - The clear colour is saved and restored.
  - The comparison now also reads state the backend never writes: element buffer, sRGB, depth function, cull mode, front face, polygon mode, stencil functions and operations, blend colour, primitive restart, alpha to coverage, uniform buffer bindings, current vertex attributes, and texture bindings of eight units.
  - `sameGlStateAround()` lets the program check Raven's whole frame and Dear ImGui's renderer.
- **Verification:** with the restore removed, 60 of 60 frames count a mismatch; with it, 0 in every run on Windows and Linux.

**3. Close and quit both sent to Raven** (host bridge)

- **Problem:** a real close (WM_CLOSE) logged "Raven error 0x0071: `Rhost_inject_close`: invalid window handle".
- **Root cause:** closing the last window gives `SDL_EVENT_WINDOW_CLOSE_REQUESTED` and at once `SDL_EVENT_QUIT`. The host injected both, and Raven had closed the window after the first. The self-test pushed only the first event, so it never saw this.
- **Fix** (`36572f8`): the quit that directly follows a close request is skipped, and nothing is sent to a window Raven no longer has. A quit on its own (SIGTERM, the session ending) is still a close request. The self-test now pushes both events.
- **Verification:** real closes on Windows (WM_CLOSE) and Linux (SIGTERM): one close callback, 0 Raven errors, exit code 0.

**4. Raven handles used after the close** (prototype loop)

- **Problem:** Linux Release logged three Raven errors (invalid window and widget handles) after the close.
- **Root cause:** an accepted close request destroys the window and its widgets inside `Rhost_inject_close`, during the event loop. The prototype's statistics (every 0.5 s, hence only sometimes) and the read-back viewport still used those handles before `Rdraw_frame` reported the end.
- **Fix** (`68797df`): both check `Rwindow_is_valid` first.
- **For the editor:** after an accepted close, every handle of that window is invalid at once.
- **Verification:** the last three Linux runs, Debug and Release: every self-test 23/23 with 0 Raven errors; Windows likewise.

**Problems of the test tools** (fixed in the tools, not in Raven or the engine):

- **xdotool typing:** typing characters outside the current layout remaps a key for each character and maps it back at once, so SDL read some keystrokes with the old keymap ("ррее, рр!"). The driver now switches the X server to a real Russian XKB layout (`6a2ffd1`).
- **xclip:** the background xclip kept the workflow step's output open, so the step never ended.
- **Workflow:** steps ran without `pipefail`.
- **Scale check:** a "150 %" Linux run actually ran at 1.0 unnoticed; `--expect-scale` now catches this (`39a2415`).
- **Xvfb reset:** `xrdb` set `Xft.dpi` and exited with 0, but Xvfb resets when its last client disconnects, and the reset removed the resource before the program started. The workflow now runs Xvfb with `-noreset` (`c439287`, `96c38d7`). With the resource in place, SDL takes the scale from `Xft.dpi`.
- **Scale-change timeout:** the program's 60 s phase timeout was too short for a person changing the display scale.

**Observations** (no fix needed for the validation):

- **Inspector at 150 %:** in a 1280×760 px window at 150 % (853×507 logical), the inspector's text fields are wider than the narrow panel and run past its edge (`002-windows-150-editor.png`). This is the prototype's layout: a 22 % panel and the TextInput's minimum width. Clicks still land exactly.
- **Text-input rectangle:** Raven offers `Rwindow_get_text_input_rect` for the caret rectangle (its Android, iOS and web platforms use it). The SDL3 host could pass it to `SDL_SetTextInputArea` for IME candidate windows. ADR 0019 condition 4 ("no IME positioning until Raven offers it") can be relaxed: Raven offers it. Not implemented or tested here (no IME).
- **Pasting several lines:** a single-line TextInput keeps the first line of a multi-line clipboard (Raven's documented behaviour). A TextEditor keeps all lines; CR LF arrives as LF.
- **Dear ImGui's scale:** it scales its style at start only (`ScaleAllSizes`, `FontScaleDpi`), so it does not follow a live scale change. The engine's own debug UI (`engine/src/debug/DebugUi.cpp`) does not scale for the display at all. Both are outside Raven.

## 6. Performance

Release, Windows, RTX 4060, 100 %, `--benchmark` (vsync off, `glFinish` per frame, 600 frames after 30 warm-up), against the evaluation's numbers:

| Configuration | Evaluation (2026-11-14) | Now | Notes |
|---|---|---|---|
| World alone, no UI | 0.237 ms | 0.238 ms | unchanged |
| Editor, draw-callback viewport, dark | 0.330 ms (`RenderFrame` 0.046 ms) | 0.346 ms (`RenderFrame` 0.052 ms) | two more widgets (Notes), the wider state comparison |
| Editor, CPU read-back viewport | 1.447 ms | 1.514 ms | read-back 0.601 + upload 0.435 ms |
| Editor, glass look | 1.114 ms | 1.215 ms | 13 backdrop copies per frame (was 12: the layout changed) |

The interface still costs about 0.1 ms per frame on top of the world. The Linux runs use a software renderer and are not measured.

## 7. Limitations

- **Linux GPU runtime: NOT VERIFIED.** No Linux machine with a GPU was available, and CI has none. Linux ran only on Mesa's software OpenGL (llvmpipe), which implements OpenGL 4.5 Core strictly but is not a hardware driver (radeonsi, iris, NVIDIA). The engine's own GPU tier has never run on Linux either.
- **Linux desktop: NOT VERIFIED.** No window manager, compositor or Wayland session; no physical Linux display scaled above 100 %. The 150 % on Linux came from `Xft.dpi` on Xvfb, the way GNOME and KDE on X11 tell programs their scale, not from a physical display; Wayland's own scaling was not tried.
- **macOS:** not a target.
- **IME:** not tested (Chinese, Japanese, Korean input); see the text-input rectangle under *Observations*.
- **Several monitors:** only one monitor. A window moved between monitors with different scales was not tried; a scale changed while the program ran was.
- **Floating panels:** natively floating (torn-off) panels still need OS windows the host platform cannot create; floating inside the window works (as in the evaluation).
- **Untested widgets:** the file browser, dialogs, accessibility and hours-long sessions were not tested.
- **Real input runs on the desktop:** on Windows it needs the desktop to itself for about a minute (it moves the pointer and types), and the user was asked not to touch it. It restores the clipboard text and the keyboard layout.

## 8. Final decision

### APPROVED

**Raven can be the main UI of the M5 editor. Dear ImGui stays the debug UI and the fallback.**

**Reasons:**

- **Every criterion holds.**
  - Builds: Windows MSVC and Linux GCC, Debug and Release, warnings as errors.
  - Runtime: Windows on a GPU; Linux on X11 with Mesa's OpenGL 4.5.
  - The OpenGL backend leaves the state exactly as it found it, by a wider comparison than before, also checked from outside the backend.
  - SDL3 input: real OS input, events not handled twice.
  - Text: typed with key presses and keyboard layouts, Cyrillic UTF-8 included.
  - The clipboard, in both directions.
  - Focus loss and restore, with nothing stuck.
  - A display set to 150 % in Windows and changed while the program ran, and Linux at 150 % through Xft.dpi as a desktop sets it.
  - Clicks to the pixel, resizing, a clean shutdown, and Dear ImGui alongside.
  - The project's regression suite passes.
- **The problems were all in the project's own integration code.** There were four: two in the host bridge, one in the OpenGL backend, one in the prototype's loop. None was in Raven. Each fix is small and local (5 to 123 lines) and covered by a check that fails without it. This is the evidence ADR 0019 asked for: the project can maintain its own OpenGL backend and SDL3 bridge (condition 1), and the real-input driver and the state comparison now guard them.
- **No architecture problem.** Raven stays an interface-level dependency: no change to scene, core, assets, scripting or the renderer API. The integration architecture of ADR 0019 stands.

**What carries into M5.** These are not blockers. The user decides them, and ADR 0019 records them when it is accepted.

1. **Linux on real graphics hardware and a real desktop.** The only part not run here: a GPU driver, a window manager or compositor, Wayland. Run it in the first M5 step that brings Raven into the engine; a critical problem there reopens the choice (ADR 0019).
2. **Keep the four fixes.** The engine's integration in `engine/src/platform/sdl3/` and `engine/src/render/opengl/` takes them over: held input released on focus loss, one close request for SDL's close and quit, no handle use after an accepted close, the clear colour restored. It also takes the wider state comparison, and the real-input driver as a check.
3. **IME positioning:** `Rwindow_get_text_input_rect` → `SDL_SetTextInputArea`. ADR 0019 condition 4 changes accordingly.
4. **The other conditions of ADR 0019 remain:** pinned vendored source with its notices (2), an unstable 0.x API behind the editor's own panel code (3), panels floating inside the window only (4), and live reload, search-path loading and the HTTP image loader reviewed (5).
5. **ADR 0019 accepted.** It was proposed when this validation ended; the user accepted the result and the ADR on 2026-11-15 (ADR 0019, *Acceptance*).
