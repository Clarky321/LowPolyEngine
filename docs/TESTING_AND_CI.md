# Testing, quality gates and CI strategy

## 1. Test pyramid

### Unit tests — always headless

Use Catch2 and register tests with CTest. Keep these tests independent of SDL window creation and GPU access.

Initial high-value test groups:

- Core: clock conversions, event dispatch invariants, result/error helpers.
- Camera: basis vectors, yaw/pitch limits, movement normalization, projection parameters.
- WorldGen: seeded PRNG, stable seed derivation, noise edge cases, island masks, height bounds, mesh construction, biome selection, placement bounds.
- Scene: stable ID uniqueness, component add/remove, hierarchy validation, serialization round-trip.
- Assets: path normalization, stable handle validation, missing-file behavior.
- Lua: valid binding calls, invalid arguments, runtime errors, expired entity handles.

### Integration tests — headless where possible

- Load an engine/project configuration.
- Create a scene, add entities/components, save and reload it.
- Run a Lua script against a controlled engine facade.
- Import a small fixture asset and verify metadata/geometry.
- Generate a world from a fixed seed and verify a stable logical summary/hash.
- Window smoke test (implemented, M0.4): `island-preview --frames 3` with `SDL_VIDEO_DRIVER=dummy`, which exercises platform init, window creation and the event loop without a display or GPU. Once the sandbox needs an OpenGL context (M0.5), context creation moves to the rendering tier.

### Rendering tests — separate optional tier

- Create a graphics context in a known runner/environment.
- Compile/link all required shaders.
- Render a tiny known scene offscreen.
- Capture diagnostic images where the rendering environment is controlled.
- Use a tolerance-based image comparison; avoid requiring byte-identical pixels across unrelated GPU drivers.

A failed rendering tier must be distinguishable from a failed unit-test tier.

## 2. Required invariants for procedural geometry

For every generated mesh, validate:

- All position, normal, UV and color values are finite.
- Every index is less than the vertex count.
- Triangle area is above a small documented epsilon, or degenerate triangles are removed.
- Normals are finite and normalized within tolerance when normalization is required.
- Bounds are finite and within configured world limits.
- Generation from the same seed/settings/version is stable.
- Distinct seeds produce variation across a representative seed corpus.
- Invalid settings fail predictably or are clamped according to a documented contract.

Avoid tests that assert fragile exact output from an algorithm still expected to evolve. Store a generator version in serialized settings and test stable behavior within that version.

## 3. Static quality checks

Recommended checks:

- `clang-format --dry-run --Werror` (or format-check script).
- `clang-tidy` on changed C++ targets.
- Compiler warnings enabled for all first-party code; progressively enforce warnings-as-errors in CI after toolchain baselines are clean.
- AddressSanitizer and UndefinedBehaviorSanitizer on a supported Linux compiler job.
- ThreadSanitizer only when concurrent code exists and supported runtime is available.
- Optional coverage report with `gcovr` or `llvm-cov`.
- Optional fuzz targets for asset importers and project serialization.

Do not turn on every analyzer for every compiler before the first build is stable; establish a known baseline, then make gates stricter.

## 4. GitHub Actions pipeline

### Pull request / push workflow

1. Checkout with required submodules only if any have actually been adopted.
2. Install the pinned developer tools (`requirements-dev.txt`: Conan, clang-format, Ninja on Linux). CMake and the compiler come from the pinned runner image.
3. Select a Conan profile and install locked dependencies.
4. Configure CMake using the documented preset.
5. Build the engine, editor (where appropriate) and tests.
6. Run CTest headless suite.
7. Run formatting/static checks.
8. Upload logs/test reports on failure.

### Initial matrix

Implemented in `.github/workflows/ci.yml` (M0.3). Runner labels are explicit rather than `*-latest`, so an image change does not silently change the toolchain.

| Runner | Compiler | Purpose |
|---|---|---|
| `windows-2025-vs2026` | MSVC 19.5x (VS 2026) | Main developer platform; Debug + Release, headless tests, format check. |
| `ubuntu-24.04` | GCC 13 | Portability and Linux build/test path; Debug + Release. |
| Optional Linux sanitizer job | Clang/GCC | ASan/UBSan and later TSAN where supported. |
| Optional self-hosted GPU runner | Known GPU/driver | OpenGL shader/context/render smoke tests. |

Add macOS when it becomes a supported target. Do not claim macOS compatibility merely because SDL3 supports macOS; the selected graphics API and editor must also be verified.

## 5. Docker

Use Docker for a pinned Linux build environment and headless tests. Keep the normal desktop editor native on the host OS. GPU-in-container testing needs explicit access to graphics devices/drivers and display infrastructure; do not make it a prerequisite for ordinary pull requests.

## 6. Developer scripts

The `.bat` wrappers should be thin interfaces over the same build graph used in CI:

Implemented in `scripts/windows/` (M0.2); shared preset mapping and tool lookup live in `_common.bat`:

- `configure.bat debug|release` — Conan install + CMake configure.
- `build.bat debug|release` — configure + build.
- `test.bat debug|release` — run CTest and return its exit code. Test presets fail when no tests are found.
- `run.bat debug|release [target]` — run a built executable from `build/<preset>/bin/<Config>/`.
- `format-check.bat` — clang-format check without modifying files.
- `clean.bat debug|release|all` — remove only known generated build directories.

Use `setlocal`, quoted paths, `if errorlevel 1 exit /b %errorlevel%` (or an equivalent robust pattern) and avoid hard-coded absolute paths. Python can provide cross-platform orchestration later; Windows batch should not reimplement dependency resolution.

## 7. Definition of a green change

A change is not “green” until:

- configure/build succeeds for the affected target;
- relevant CTest tests pass;
- format/static checks are clean or documented;
- the PR workflow passes on the required matrix;
- a manual GPU smoke test is recorded when a change affects windowing, shaders, resource lifetime or rendering;
- the author/agent reports exactly which commands ran and which checks were not run.
