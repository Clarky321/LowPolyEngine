# LowPolyEngine

A C++20 low-poly 3D engine and editor with Lua gameplay scripting. Current stage: **M1 — engine loop and camera** is implemented (application loop, frame timing, keyboard/mouse input, free-fly camera, mesh rendering from CPU `MeshData`, debug overlay); M0 (build, CI, SDL3 window, OpenGL 4.5 context) is done. Next is **M2 — low-poly renderer and materials**. See `docs/ENGINE_PLAN.md` for the roadmap.

![island-preview with the M1 debug overlay](docs/screenshots/m1/004-debug-overlay.png)

## Running the island preview

```powershell
scripts\windows\build.bat debug
scripts\windows\run.bat debug
```

| Input | Action |
|---|---|
| hold right mouse button + move | look around |
| W / A / S / D (or arrow keys) | fly forward / left / back / right |
| E or Space / Q or Ctrl | up / down |
| Shift | fly faster |
| mouse wheel | change flying speed |
| F1 | toggle the debug overlay |
| F12 | save a screenshot to `screenshots/` (ignored by Git) |
| Escape | quit |

Keys are physical positions, so WASD works on any keyboard layout. Command-line options: `island-preview [--frames N] [--assets DIR] [--screenshot FILE.png]`; `--screenshot` saves the last frame (frame 120 unless `--frames` is given) and exits. This is how the images in `docs/screenshots/` are made (ADR 0006).

## Building on Windows

### Requirements

| Tool | Version used | Notes |
|---|---|---|
| Visual Studio 2026 (MSVC 19.5x) | 18.x | "Desktop development with C++" workload. Ships CMake, Ninja, clang-format and clang-tidy. |
| CMake | ≥ 3.25 | The one bundled with Visual Studio works. |
| Python | 3.10+ | Only for the developer tools below. |
| Conan, clang-format | pinned in `requirements-dev.txt` | Installed into a local `.venv`, see below. CI uses the same versions. |

### First-time setup

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements-dev.txt
```

### Build and test with the scripts

Thin wrappers in `scripts/windows/` run the same commands as below. Each takes `debug` (default) or `release`; they work from any folder.

| Script | Does |
|---|---|
| `configure.bat [debug\|release]` | `conan install` + `cmake --preset` |
| `build.bat [debug\|release]` | configure, then `cmake --build --preset` |
| `test.bat [debug\|release] [gpu]` | `ctest --preset` on an existing build: headless tests, or with `gpu` only the rendering tests (need OpenGL 4.5) |
| `run.bat [debug\|release] [target]` | runs `build\<preset>\bin\<Config>\<target>.exe` from the repository root; default target `island-preview` (free-fly test scene) |
| `format-check.bat` | `clang-format --dry-run --Werror` on all C/C++ sources |
| `clean.bat [debug\|release\|all]` | deletes `build\windows-msvc-*` only (default: all) |

Conan and clang-format are taken from `.venv` first; other tools from `PATH`, then from the Visual Studio installation (CMake, clang-format).

### Build and test manually (Debug)

```powershell
.venv\Scripts\conan install . -pr:h=profiles/windows-msvc -pr:b=profiles/windows-msvc -s build_type=Debug --build=missing -of build/windows-msvc-debug/conan
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

For Release, replace `Debug` with `Release` and `windows-msvc-debug` with `windows-msvc-release`. Conan must run before CMake: it generates the toolchain file the preset points to.

Options: `-DLPE_BUILD_TESTS=OFF` skips tests; `-DLPE_WARNINGS_AS_ERRORS=ON` (or the environment variable `LPE_WARNINGS_AS_ERRORS=ON`, which CI sets) turns warnings into errors.

## Building on Linux

Supported baseline: Ubuntu 24.04 with GCC 13 (what CI uses). Ninja comes from `requirements-dev.txt`.

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
export PATH="$PWD/.venv/bin:$PATH"
conan install . -pr:h=profiles/linux-gcc -pr:b=profiles/linux-gcc -s build_type=Debug --build=missing -of build/linux-gcc-debug/conan
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

SDL needs X11 and OpenGL development packages (`libx11-dev`, `libgl-dev` and related). Conan only checks for them by default; add `-c tools.system.package_manager:mode=install -c tools.system.package_manager:sudo=True` to `conan install` to let it install them with apt (CI does this).

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request: Windows MSVC (`windows-2025-vs2026` runner, through `scripts/windows/*.bat`) and Linux GCC 13 (`ubuntu-24.04`, the commands above), Debug and Release, with warnings as errors. The Windows Debug job also runs `format-check.bat`. CI runs the headless tiers only; the GPU tier (`test.bat <config> gpu` or the `*-gpu` test presets) is run locally.

## Repository layout

- `engine/` — engine library (`engine` CMake target), one folder per domain:
  - `app/` — application lifecycle and frame loop (`Application`, `run`).
  - `core/` — results, logging, frame clock, files, PNG images, math aliases (GLM).
  - `platform/` — window, OpenGL context, input; SDL3 only in `engine/src/platform/sdl3/`.
  - `geometry/` — CPU-side `MeshData` and primitive builders; no GPU or SDL code.
  - `render/` — renderer API with mesh handles; OpenGL/GLAD only in `engine/src/render/opengl/`.
  - `camera/` — camera math and the free-fly controller.
  - `debug/` — built-in pixel font for on-screen diagnostics.
- `sandbox/island-preview/` — first executable: free-fly test scene with debug overlay (see above; `assets/` is found relative to the working directory).
- `assets/shaders/` — GLSL sources loaded at runtime.
- `tests/` — Catch2/CTest tiers: `unit/` (headless), `integration/` (SDL dummy video driver, headless), `rendering/` (label `gpu`, real OpenGL, frame readback).
- `docs/screenshots/` — captured screenshots per milestone, with how they were made.
- `third_party/glad/` — generated OpenGL loader; regenerate with `scripts/python/generate_glad.py` (ADR 0005).
- `third_party/stb_image_write/` — compiles the stb PNG writer (header from Conan) without project warning flags (ADR 0006).
- `cmake/` — shared CMake modules (warning policy).
- `profiles/` — Conan profiles.
- `docs/` — plan, tech stack, structure, testing strategy, ADRs.
- `CLAUDE.md`, `AGENTS.md`, `.claude/skills/engine-dev/` — instructions for AI-assisted development.
