# Testing, quality gates and CI strategy

## 1. Test pyramid

### Unit tests — always headless

Use Catch2 and register tests with CTest. Keep these tests independent of SDL window creation and GPU access.

Initial high-value test groups:

- Core: clock conversions, event dispatch invariants, result/error helpers, PNG encoding and decoding (every color type, size limits, corrupted and truncated files; implemented, `tests/unit/core/`).
- Camera: basis vectors, yaw/pitch limits, movement normalization, projection parameters.
- Render (GPU-free parts): handle pool, material and light validation, the normal matrix (implemented, `tests/unit/render/`).
- Sandbox: the textures and the model island-preview loads decode and load cleanly, since island-preview itself only logs a broken asset, and its sky dome (gradient stops, unit sphere with a horizon ring, inward faces) is checked (implemented, `tests/unit/sandbox/`).
- Geometry: `MeshData` validation and appending, primitives, face normals, OBJ subset import with inline fixtures for every face form, negative indices, polygons, missing normals, malformed lines with their line number, out-of-range indices, limits, NaN/Inf, empty input and CRLF (implemented, `tests/unit/geometry/`).
- WorldGen: seeded PRNG and stable seed derivation with golden values that match on every platform, gradient noise, fBm, ridge noise and domain warp with golden samples, ranges, continuity and parameter edge cases, world generation settings with every field below, at and above its limits and the combination rules, 3D island placement with golden layouts, bounds, spacing, variation and the policy for islands that do not fit, island shapes (outline, mask, top and underside bounds, the shared rim, the not-a-disc/not-a-cone criteria over a seed corpus), island meshes (validity, winding, closure along the rim, minimum triangle area, bounds, golden hashes), palettes (validation, thresholds, stone strata, neighbour rule, variety, painting), local water (golden lakes, streams and waterfalls; lakes enclosed by terrain at or above their level, streams that never rise from a spring through their ponds to the rim, waterfalls at the rim and clear of other islands, settings switches, dry islands without relief), clouds (golden clusters; sizes, altitudes and densities within the settings, inside the bounds, clear of the islands; meshes inside their volume; the drift formula with wrapping), whole archipelagos (implemented, `tests/unit/worldgen/`).
- Scene: stable ID uniqueness, component add/remove, hierarchy validation, serialization round-trip.
- Assets: path normalization, stable handle validation, missing-file behavior.
- Lua: valid binding calls, invalid arguments, runtime errors, expired entity handles.

### Integration tests — headless where possible

- Load an engine/project configuration.
- Create a scene, add entities/components, save and reload it.
- Run a Lua script against a controlled engine facade.
- Import a small fixture asset and verify metadata/geometry.
- Generate a world from a fixed seed and verify a stable logical summary/hash.
- Platform layer (implemented, `tests/integration/`): SDL init/shutdown, window creation and error paths, event polling, keyboard/mouse/focus events pushed into SDL's queue and translated into `InputState`, application startup failure without a GL-capable driver — all with `SDL_VIDEO_DRIVER=dummy` set by CTest, no display or GPU. Label `integration`; part of the default test presets and CI.

### Rendering tests — separate optional tier

Implemented in `tests/rendering/` with CTest label `gpu`. The default test presets exclude the label; `ctest --preset <preset>-gpu` (or `scripts\windows\test.bat <config> gpu`) runs only this tier, so a failure is never mixed with the headless results. Current tests:

- `lpe_gpu_tests` (Catch2): the application loop (callback order, quit, startup errors, screenshot files) and the renderer checked by reading back pixels with a small tolerance — clear color, vertex colors, back-face culling, depth order, model/view/projection order, alpha blending, mesh updates, stale handles, invalid mesh data, overlay pixel orientation, optional normals and UVs (test-only shaders in `tests/rendering/fixtures/` show them as color; meshes without them read the defaults; updates add and remove them), textures (`texture_gpu_test.cpp` inspects the OpenGL objects: handles, release on destruction, row order, sampling state and mip levels), materials (base color tint, default material, alpha, invalid handles), texture sampling (exact texels with nearest filtering, a linear ramp within tolerance, repeat versus clamp, texture × vertex color × base color, texture alpha, textured materials on meshes without UVs, destroyed textures), lighting (ambient plus Lambert diffuse, per-channel light colors, unlit materials, the per-frame light reset, normals under rotation and non-uniform scale, lit materials on meshes without normals, invalid lights).
- `island-preview` smoke runs: 30 frames of the generated archipelago (the default scene), of `--seed 42` and of `--test-scene`, each without OpenGL errors and without any logged error (every test-scene asset loads); the archipelago log line names the seed, 12 of 12 islands, the triangles, the lakes, streams and waterfalls, the 20 cloud clusters and the generation time; `--island 0` shows one island close up with its translucent water, and an island the world does not have is named; options of one scene given to the other print the usage; a broken model file (`fixtures/broken-model/`) is logged with its line and the scene still runs, a missing one is named; `--screenshot` writes a PNG; broken-shader fixtures must produce compile, link, missing-file, missing-uniform, missing-base-color, missing-base-color-texture, missing-lighting and wrong-attribute-location messages that name the shader file or variable.

Screenshots for documentation use the same capture path (`island-preview --screenshot FILE.png`, ADR 0006) and are stored per milestone in `docs/screenshots/`, with the command and commit recorded next to them.

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
- the checks that §8 requires for the kind of change have run;
- the author/agent reports exactly which commands ran and which checks were not run.

## 8. Checks per sub-milestone

Every sub-milestone task (`docs/DEVELOPMENT_WORKFLOW.md`) runs these checks before its commit and reports the exact commands and results. Commands are for Windows, from the repository root; on Linux use the `README.md` commands with the `linux-gcc-*` presets.

| Check | Command | Required when the change touches |
|---|---|---|
| Debug build, warnings as errors | `$env:LPE_WARNINGS_AS_ERRORS = "ON"` (PowerShell) or `set LPE_WARNINGS_AS_ERRORS=ON` (cmd), then `scripts\windows\build.bat debug` | code, shaders, CMake, Conan or scripts |
| Headless tests (unit + integration) | `scripts\windows\test.bat debug` | code, shaders, CMake, Conan or scripts |
| Format check | `scripts\windows\format-check.bat` | code |
| Whitespace | `git diff --check`, and `git diff --cached --check` after staging | anything, documentation included |
| GPU tier, Debug | `scripts\windows\test.bat debug gpu` | rendering, shaders, GPU resources, windowing, the application loop, or code the renderer calls (such as `geometry::validate`) |
| Release build + headless tests | `scripts\windows\build.bat release`, then `scripts\windows\test.bat release` | resource lifetime or ownership, build files or dependencies; always in the final sub-milestone |
| GPU tier, Release | `scripts\windows\test.bat release gpu` | the final sub-milestone; a Release build that changed rendering code |
| Screenshot | `build\windows-msvc-debug\bin\Debug\island-preview.exe --screenshot docs\screenshots\mN\NNN-name.png` | anything visible on screen (`docs/DEVELOPMENT_WORKFLOW.md` §6) |
| Links and names | every relative link, file, script, preset and command named in changed documents exists | documentation |
| CI | push, then the run for that commit is green on all jobs | anything |

Notes:

- The presets read `LPE_WARNINGS_AS_ERRORS` from the environment at configure time, and `build.bat` configures on every run, so setting it in the same shell is enough. CI sets it for all jobs.
- CI runs neither the GPU tier nor screenshots (no GPU runner). Their results come only from the local run and must be in the report, including the GPU and driver used.
- A check that does not apply is reported as "not required" with the reason; a check that applies but could not run is reported as "not run", never as passed.
