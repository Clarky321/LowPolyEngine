# Raven UI evaluation prototype

An experiment, not part of the engine: it checks whether [Raven](https://github.com/oscar-soirey/Raven) can be the UI framework of the M5 editor on top of the engine's SDL3 window, OpenGL 4.5 context and renderer. Plan, results and conclusion: `docs/experiments/raven-ui-evaluation.md`.

It is built only on request (`LPE_BUILD_RAVEN_EVALUATION=ON`); the default presets, the `.bat` wrappers and CI never build it, and nothing in `engine/` depends on it.

## What it contains

| File | Purpose |
|---|---|
| `fetch_raven.py` | Fetches Raven at the pinned commit `7ab270df3b618de3d11af6033912374b6cd621b7` into `build/external/raven` (ignored by Git). Builds and runs nothing from the checkout. |
| `CMakeLists.txt` | Compiles Raven from that checkout as a static C library (Vulkan, web pages, examples, tests and tools off; nothing downloaded) and the `raven-evaluation` program. |
| `GlRavenBackend.*` | An OpenGL 4.5 Core backend for Raven (`Rbackend_register`, `R_BACKEND_CUSTOM`): Raven's reference shaders adapted at load time, DSA textures, instanced draws, viewport callbacks, backdrop copies for glass, full save and restore of the OpenGL state. |
| `SdlRavenHost.*` | Raven's host platform on the SDL3 window: mouse, wheel, keys (layout-aware with a position fallback), UTF-8 text, focus, mouse leave, size and scale, close; cursor and clipboard through SDL. |
| `main.cpp` | One window, one loop: the engine's renderer, island-preview's `WorldScene` and sky, Raven with its render thread disabled, the editor shell, a self-test, a stress test and a benchmark. |
| `editor.xml` | The editor shell: toolbar, viewport, hierarchy, inspector (with a multi-line Notes editor) and console in a `DockArea`. |
| `input_driver.py` | Real OS input for `--input-check` (final validation, `docs/experiments/raven-final-validation.md`): SendInput, the Win32 clipboard and window focus on Windows; xdotool, setxkbmap and xclip on X11. |

## Build and run (Windows, Visual Studio 2026)

From the repository root, after the usual first-time setup (`README.md`) and one normal configure, so that the Conan files of the preset exist (`scripts\windows\configure.bat debug`, likewise `release`):

```powershell
.venv\Scripts\python sandbox\raven-evaluation\fetch_raven.py
$env:LPE_WARNINGS_AS_ERRORS = "ON"
cmake --preset windows-msvc-debug -B build/raven-eval-debug -DLPE_BUILD_RAVEN_EVALUATION=ON -DLPE_BUILD_TESTS=OFF
cmake --build build/raven-eval-debug --config Debug --target raven-evaluation
build\raven-eval-debug\bin\Debug\raven-evaluation.exe
```

For Release, use `windows-msvc-release`, `build/raven-eval-release` and `--config Release`. A separate build folder keeps the normal preset folders untouched.

## Options

| Option | Effect |
|---|---|
| (none) | The editor: hold the right mouse button in the viewport to look around, WASD/Q/E to fly (after a click in the viewport), the wheel for speed; Regenerate (Ctrl+R), viewport mode, Screenshot (F12); the hierarchy selects an island for the inspector. |
| `--self-test` | Scripted input pushed into SDL's event queue and checked step by step (23 steps); exit code 0 when all pass and no OpenGL error, state mismatch, Raven error or leak was seen. |
| `--stress N` | Raven started and stopped N times, with regeneration, resizes and both viewport modes in each cycle; checks Raven's allocation count after `Rshutdown`, the backend's textures and the renderer's resources. |
| `--benchmark` | Frame times (with `glFinish`) of the world alone, the editor with the draw-callback viewport, the CPU read-back viewport, and the glass look. Turns vsync off. |
| `--frames N`, `--screenshot FILE.png` | Run N frames and save the last one. |
| `--texture-mode` | Start with the CPU read-back viewport. |
| `--look dark\|light\|glass\|windows` | Raven's look. |
| `--ui-scale F` | Raven's UI scale (0.25–4) on top of the display scale. |
| `--input-check` | Phases driven by real OS input from `input_driver.py` (start it through the driver, not by hand): clicks, typing, editing keys, the clipboard, focus loss, resize, close; exit code 0 when every check passed. |
| `--with-imgui` | Dear ImGui's debug UI drawn after Raven's frame on the same window and context; events go to Dear ImGui first. Works with every mode. |
| `--scale-change` | With `--input-check`: a phase that waits for the display scale to be changed in the system settings while the program runs. |

## Real input check

Builds as above, then (Windows; it moves the pointer and types for about a minute, do not use the mouse or the keyboard meanwhile; it restores the clipboard text and the keyboard layout):

```powershell
.venv\Scripts\python sandbox\raven-evaluation\input_driver.py build\raven-eval-release\bin\Release\raven-evaluation.exe --with-imgui
```

On Linux the same command runs on an X server (`DISPLAY`) with `xdotool`, `setxkbmap`, `xclip` and `xmessage` installed; `.github/workflows/raven-validation.yml` does it on Xvfb for Debug and Release, at 100 % and 150 %.

## Building on Linux

After the usual Linux setup (`README.md`, "Build and test manually") and `conan install` for the preset:

```bash
python sandbox/raven-evaluation/fetch_raven.py
cmake --preset linux-gcc-debug -B build/raven-eval-debug -DLPE_BUILD_RAVEN_EVALUATION=ON -DLPE_BUILD_TESTS=OFF
cmake --build build/raven-eval-debug --target raven-evaluation
build/raven-eval-debug/bin/raven-evaluation --self-test
```
| `--size WxH`, `--seed N`, `--no-vsync` | Window size, world seed, vsync off. |
