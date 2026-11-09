# LowPolyEngine

A C++20 low-poly 3D engine and editor with Lua gameplay scripting. Current stage: **M0 — foundation** (build system and tests). See `docs/ENGINE_PLAN.md` for the roadmap.

## Building on Windows

### Requirements

| Tool | Version used | Notes |
|---|---|---|
| Visual Studio 2026 (MSVC 19.5x) | 18.x | "Desktop development with C++" workload. Ships CMake, Ninja, clang-format and clang-tidy. |
| CMake | ≥ 3.25 | The one bundled with Visual Studio works. |
| Python | 3.10+ | Only to run Conan. |
| Conan | 2.x | Installed into a local `.venv`, see below. |

### First-time setup

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install conan
```

### Build and test with the scripts

Thin wrappers in `scripts/windows/` run the same commands as below. Each takes `debug` (default) or `release`; they work from any folder.

| Script | Does |
|---|---|
| `configure.bat [debug\|release]` | `conan install` + `cmake --preset` |
| `build.bat [debug\|release]` | configure, then `cmake --build --preset` |
| `test.bat [debug\|release]` | `ctest --preset` on an existing build |
| `run.bat [debug\|release] [target]` | runs `build\<preset>\bin\<Config>\<target>.exe` from the repository root; default target `island-preview` (added in M0.4) |
| `format-check.bat` | `clang-format --dry-run --Werror` on all C/C++ sources |
| `clean.bat [debug\|release\|all]` | deletes `build\windows-msvc-*` only (default: all) |

Tools are taken from `PATH` first, then from the Visual Studio installation (CMake, clang-format). Conan is taken from `.venv` first.

### Build and test manually (Debug)

```powershell
.venv\Scripts\conan install . -pr:h=profiles/windows-msvc -pr:b=profiles/windows-msvc -s build_type=Debug --build=missing -of build/windows-msvc-debug/conan
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

For Release, replace `Debug` with `Release` and `windows-msvc-debug` with `windows-msvc-release`. Conan must run before CMake: it generates the toolchain file the preset points to.

Options: `-DLPE_BUILD_TESTS=OFF` skips tests; `-DLPE_WARNINGS_AS_ERRORS=ON` turns warnings into errors.

## Repository layout

- `engine/` — engine library (`engine` CMake target).
- `tests/` — headless Catch2 unit tests registered with CTest.
- `cmake/` — shared CMake modules (warning policy).
- `profiles/` — Conan profiles.
- `docs/` — plan, tech stack, structure, testing strategy, ADRs.
- `CLAUDE.md`, `AGENTS.md`, `.claude/skills/engine-dev/` — instructions for AI-assisted development.
