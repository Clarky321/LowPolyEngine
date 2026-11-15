# Testing, quality gates and CI strategy

## 1. Test pyramid

### Unit tests — always headless

Use Catch2 and register tests with CTest. Keep these tests independent of SDL window creation and GPU access.

Initial high-value test groups:

- Core: clock conversions, event dispatch invariants, result/error helpers, PNG encoding and decoding (every color type, size limits, corrupted and truncated files; implemented, `tests/unit/core/`).
- Camera: basis vectors, yaw/pitch limits, movement normalization, projection parameters.
- Render (GPU-free parts): handle pool, material and light validation, the normal matrix (implemented, `tests/unit/render/`).
- Sandbox: the textures and the model island-preview loads decode and load cleanly, since island-preview itself only logs a broken asset, and its sky dome (gradient stops, unit sphere with a horizon ring, inward faces) is checked (implemented, `tests/unit/sandbox/`). The WorldGen panel's state without UI (groups, clamping, regeneration only of valid settings, revert, defaults, save and load) is tested headless too (`tests/unit/sandbox/worldgen_panel_test.cpp`).
- Geometry: `MeshData` validation and appending, primitives, face normals, OBJ subset import with inline fixtures for every face form, negative indices, polygons, missing normals, malformed lines with their line number, out-of-range indices, limits, NaN/Inf, empty input and CRLF (implemented, `tests/unit/geometry/`).
- WorldGen: seeded PRNG and stable seed derivation with golden values that match on every platform, gradient noise, fBm, ridge noise and domain warp with golden samples, ranges, continuity and parameter edge cases, world generation settings with every field below, at and above its limits and the combination rules, 3D island placement with golden layouts, bounds, spacing, variation and the policy for islands that do not fit, island shapes (outline, mask, top and underside bounds, the shared rim, the not-a-disc/not-a-cone criteria over a seed corpus), island meshes (validity, winding, closure along the rim, minimum triangle area, bounds, golden hashes), palettes (validation, thresholds, stone strata, neighbour rule, variety, painting), local water (golden lakes, streams and waterfalls; lakes enclosed by terrain at or above their level, streams that never rise from a spring through their ponds to the rim, waterfalls at the rim and clear of other islands, settings switches, dry islands without relief), clouds (golden clusters; sizes, altitudes and densities within the settings, inside the bounds, clear of the islands; meshes inside their volume; the drift formula with wrapping), settings fields for editors (every field once, bound to the path validation names), settings files (round trip of every field bit for bit, seeds above 2^53, defaults for missing fields, warnings for unknown ones, wrong types, malformed JSON with line and column, newer format versions, validation, the shipped default file, the same world from a loaded file), whole archipelagos, and a seed corpus of 16 whole worlds that are valid in every part, all different, repeatable, with golden fingerprints of seeds 1 and 42 checked on Windows and Linux (implemented, `tests/unit/worldgen/`).
- Scene: stable ID uniqueness, component add/remove, hierarchy validation, serialization round-trip. Implemented (M4.1): persistent IDs — golden IDs of the seeded generator, version and variant bits, uniqueness, text round trip, every parse error, separate ID types (`tests/unit/core/uuid_test.cpp`); UTF-8 checks (`tests/unit/core/utf8_test.cpp`); the scene core — names, create, find, destroy, unknown, null and duplicate IDs, creation order, `clear`, moving, 100 000 entities (`tests/unit/scene/scene_test.cpp`). M4.2: every component's rules and messages, and the camera limits against `camera::projectionMatrix` (`tests/unit/scene/components_test.cpp`); add, read, replace and remove of each component type, duplicates, invalid values that change nothing, unknown, null and destroyed entities, queries by one and two components in creation order, components gone with their entity, normalized rotations and light directions that store again bit for bit, 100 000 entities with components (`tests/unit/scene/scene_components_test.cpp`); no EnTT or nlohmann_json include in a public header (`tests/unit/scene/public_headers_test.cpp`). M4.4: scene documents — the exact layout, round trip with IDs, order, components and floats bit for bit (`-0.0`, subnormals), the same bytes when written again, defaults and warnings, every envelope, entity and component error with its path, asset references with and without a registry, the entity and size limits at and above their value, files saved, replaced, refused and loaded (`tests/unit/scene/scene_document_test.cpp`); the migration hook with a test-only document (`tests/unit/core/document_migration_test.cpp`); atomic file replacement, missing folders and read-only targets (`tests/unit/core/atomic_write_test.cpp`); a scene with asset references saved, destroyed and loaded with the project's registry, a failed save that leaves the old file, a broken file that leaves the loaded scene (`tests/integration/scene_document_test.cpp`). M4.6: parent links — null, self, a parent from another scene, cycles through add and replace, children detached on destroy, links in scene documents with their errors (`tests/unit/scene/parent_component_test.cpp`); prefabs — made from entities with links, a closed set only, every validation rule, two independent instances with links inside each, a failed instantiation that changes nothing, a scene with instances read back (`tests/unit/scene/prefab_test.cpp`); prefab documents — the exact layout, round trip, local ID and link errors, registry checks, files saved, loaded through the registry, an ID that disagrees with the registry, broken files (`tests/unit/scene/prefab_document_test.cpp`); a prefab registered in a project, instantiated twice into a loaded scene, saved and loaded again (`tests/integration/prefab_project_test.cpp`).
- Assets: path normalization, stable handle validation, missing-file behavior. Implemented (M4.3): asset IDs apart from renderer handles, entity IDs, integers and pointers (compile-time checks), type names and extensions (`tests/unit/assets/asset_id_test.cpp`); path normalization and every rejection rule — empty, too long, non-ASCII, control characters, drive letters, absolute and UNC paths, `~`, backslashes, characters Windows forbids, trailing dots and spaces, device names, `..` leaving the root (`tests/unit/assets/asset_path_test.cpp`); the registry — register, find by ID and path, duplicate IDs and paths, letter-case conflicts of files and folders, extensions, unregistering, type checks, missing files and a wrong letter case on disk, a moved root, meshes and textures resolved and read by `geometry::readObj` and `core::readPng` (`tests/unit/assets/asset_registry_test.cpp`); the manifest — exact text, round trip, missing and unknown fields, wrong types, malformed JSON, format and version, invalid entries, conflicting entries, the entry and file size limits (`tests/unit/assets/asset_manifest_test.cpp`); a scene's unknown and wrongly typed asset references (`tests/unit/scene/asset_references_test.cpp`); a project folder saved, moved and loaded again (`tests/integration/asset_project_test.cpp`).
- Lua: valid binding calls, invalid arguments, runtime errors, expired entity handles. Implemented (M4.5): the VM — all memory returned on close, too little memory, the instruction budget (also against `pcall` in a loop), the memory limit, exactly the documented sandbox, separate environments, text-only chunks, C++ exceptions turned into errors (`tests/unit/scripting/lua_vm_test.cpp`); the runtime — a script moving its entity, reading names, transforms and the delta time, syntax and runtime errors with name, line and traceback, every argument and entity error, the API outside start and update, destroyed entities, endless loops and memory hunger stopped while other scripts run on, size, bytecode and result limits, isolation between scripts, restart with memory returned, scripts loaded through the asset registry, limits (`tests/unit/scripting/script_runtime_test.cpp`); a saved scene with script components run and saved again (`tests/integration/script_scene_test.cpp`).
- UI (Raven, M5.1, ADR 0026): Raven runs in the unit tier with its null backend (`R_BACKEND_NULL`) and a test host platform. Every input comes through `Rhost_inject_*`; there is no SDL, OpenGL or display, and no render or worker threads. Raven's state is per process, so a test holds one Raven at a time (`tests/unit/ui/RavenSession.hpp`). Implemented in `tests/unit/ui/raven_null_backend_test.cpp`:
  - the pinned version and commit against Raven and the vendored README;
  - a window from XML text with a click, typed ASCII and Cyrillic text, Backspace and a button callback that changes a label;
  - failed starts (Vulkan, WebGL, custom and unknown backends, a second `Rinit`, calls before `Rinit`) and broken XML;
  - five start and stop cycles;
  - no allocation left and an intact heap after every `Rshutdown`.

  The build configuration in `tests/unit/ui/raven_configuration_test.cpp`:
  - no native window system;
  - live reload off, so a changed file is not reloaded until `Rxml_check_files`;
  - auto-loading off, so a declaration in the base path is not taken;
  - the HTTP(S) download compiled out: no `winhttp.dll` on Windows, no `curl` process on Linux.

  M5.2: the editor shell's layout laid out at 100 %, 150 % and 200 % (also a small window at 150 %), and changed from 100 % to 150 % and back while open; every area has a size, lies inside the window, and the panels do not overlap (`tests/unit/ui/shell_layout_test.cpp`).
- Editor (M5.2): lpe-editor's options — defaults, `--frames`, `--screenshot`, `--size`, and every unknown argument, missing value, invalid count and invalid size with its message (`tests/unit/editor/editor_options_test.cpp`).

### Integration tests — headless where possible

- Load an engine/project configuration.
- Create a scene, add entities/components, save and reload it.
- Run a Lua script against a controlled engine facade.
- Scene data model (implemented, M4.3–M4.7, `tests/integration/`): a project folder moved with its manifest (`asset_project_test.cpp`); scenes saved, destroyed and loaded with the project's registry, failed saves and broken files (`scene_document_test.cpp`); scripts of a saved scene run and saved again (`script_scene_test.cpp`); a prefab registered and instantiated into a loaded scene (`prefab_project_test.cpp`); and the M4 exit criterion end to end (`scene_data_model_test.cpp`): a scene with every component type and asset references through the registry, a prefab made from three of its entities, saved, destroyed and loaded with every ID, value and reference intact, its Lua scripts run with a failing one contained, the prefab instantiated twice, saved and loaded again, with a golden fingerprint of the final document that CI checks on Linux. These tests draw every ID from seeded generators.
- Import a small fixture asset and verify metadata/geometry.
- Generate a world from a fixed seed and verify a stable logical summary/hash.
- Platform layer (implemented, `tests/integration/`): SDL init/shutdown, window creation and error paths, event polling, keyboard/mouse/focus events pushed into SDL's queue and translated into `InputState`, application startup failure without a GL-capable driver, `run()` refusing the Raven UI together with the debug UI — all with `SDL_VIDEO_DRIVER=dummy` set by CTest, no display or GPU. Label `integration`; part of the default test presets and CI.
- Raven UI (implemented, M5.2, `tests/integration/raven_ui_test.cpp`): the engine's Raven UI (ADR 0027) on a window of SDL's dummy driver with Raven's null backend, SDL events pushed into SDL's queue and read by `Platform::pollEvents`:
  - keys mapped by meaning and by position (AZERTY, a Cyrillic layout), modifiers, buttons; UTF-8 text to code points with U+FFFD for invalid bytes;
  - with the UI, clicks, text and keys change Raven's widgets and never reach `InputState`; without it, the same events do;
  - focus loss releases held keys and buttons once each, modifiers last, also when SDL released some keys itself; nothing stays held in Raven, the host or `InputState`;
  - a close request followed by a quit reaches Raven once: accepted, the window closes, its handles are not used again and later events go to `InputState`; refused, the window stays and the quit after it is not a second request; a quit on its own is a close request;
  - the window's pixel size and display scale reach Raven, also after a resize and a scale event;
  - the IME area follows Raven's caret in window points, SDL text input is on only while a text field has the keyboard;
  - the lifetime: live reload and auto-loading off before the first document, one UI at a time, a layout Raven cannot build (named in the error) or whose root is not a window leaves nothing running, start and stop repeated, no Raven allocation after each shutdown.
- lpe-editor's command line (M5.2, `tests/CMakeLists.txt`): an unknown argument, a bad size, a bad frame count and a missing value end with the usage line before any window exists.

### Rendering tests — separate optional tier

Implemented in `tests/rendering/` with CTest label `gpu`. The default test presets exclude the label; `ctest --preset <preset>-gpu` (or `scripts\windows\test.bat <config> gpu`) runs only this tier, so a failure is never mixed with the headless results. Current tests:

- `lpe_gpu_tests` (Catch2): the application loop (callback order, quit, startup errors, screenshot files) and the renderer checked by reading back pixels with a small tolerance — clear color, vertex colors, back-face culling, depth order, model/view/projection order, alpha blending, mesh updates, stale handles, invalid mesh data, overlay pixel orientation, optional normals and UVs (test-only shaders in `tests/rendering/fixtures/` show them as color; meshes without them read the defaults; updates add and remove them), textures (`texture_gpu_test.cpp` inspects the OpenGL objects: handles, release on destruction, row order, sampling state and mip levels), materials (base color tint, default material, alpha, invalid handles), texture sampling (exact texels with nearest filtering, a linear ramp within tolerance, repeat versus clamp, texture × vertex color × base color, texture alpha, textured materials on meshes without UVs, destroyed textures), lighting (ambient plus Lambert diffuse, per-channel light colors, unlit materials, the per-frame light reset, normals under rotation and non-uniform scale, lit materials on meshes without normals, invalid lights).
- `island-preview` smoke runs: 30 frames of the generated archipelago (the default scene), of `--seed 42` and of `--test-scene`, each without OpenGL errors and without any logged error (every test-scene asset loads); the archipelago log line names the seed, 12 of 12 islands, the triangles, the lakes, streams and waterfalls, the 20 cloud clusters and the generation time; `--island 0` shows one island close up with its translucent water, and an island the world does not have is named; `--settings assets/worlds/default.json` gives the default world without warnings, `--save-settings` writes a file, a settings file with a wrong type or an unknown field is reported with the field path, a missing one is named; `--panel` runs with the WorldGen panel open without errors; options of one scene given to the other print the usage; a broken model file (`fixtures/broken-model/`) is logged with its line and the scene still runs, a missing one is named; `--screenshot` writes a PNG; broken-shader fixtures must produce compile, link, missing-file, missing-uniform, missing-base-color, missing-base-color-texture, missing-lighting and wrong-attribute-location messages that name the shader file or variable.

- Editor and Raven UI (M5.2, `editor_gpu_test.cpp`): the editor shell drawn by Raven through the engine's OpenGL backend, checked by its pixels (no pixel of the renderer's clear colour left, the viewport black, the four panels in the theme's panel colour, a titled tab above each docked area, every area inside the frame), with 0 OpenGL errors, 0 state mismatches, 0 Raven errors and 0 Raven allocations after shutdown; three runs in a row; an application without the UI never starts Raven; the OpenGL state comparison itself (it sees a changed clear colour, and state outside the restored group only in the full comparison); Dear ImGui's renderer leaving the full OpenGL state unchanged.
- `lpe-editor` runs: 60 frames without an error or a warning and with exit code 0 (the exit code also covers OpenGL errors, state mismatches, Raven errors and Raven memory after shutdown); `--size 1024x640 --screenshot FILE` writes a 1024x640 PNG.

**Where the GPU tier runs, kept apart in every report:**
- **Windows, hardware OpenGL:** locally on the developer machine (`scripts\windows	est.bat <config> gpu`; currently an NVIDIA GeForce RTX 4060). The Windows CI runners have no GPU and do not run this tier.
- **Linux, software OpenGL (from M5.2):** in CI, GitHub Actions `ubuntu-24.04`, Debug and Release, under Xvfb with Mesa's llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`; the job checks that `glxinfo` names llvmpipe and says so in a notice). It also runs `lpe-editor` at 100 % and at 150 % (`Xft.dpi: 144`, which SDL reads as the display scale) and keeps the logs and screenshots as an artifact. This is software rendering on the CPU: it proves the code paths and the OpenGL 4.5 Core API use, not a hardware driver, and never stands in for a hardware test.
- **Linux, hardware OpenGL:** not available in M5 (plan decision 2); recorded as not verified.

Screenshots for documentation use the same capture path (`island-preview --screenshot FILE.png`, `lpe-editor --screenshot FILE.png`, ADR 0006) and are stored per milestone in `docs/screenshots/`, with the command and commit recorded next to them.

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
| `ubuntu-24.04` | GCC 13 | Portability and Linux build/test path; Debug + Release; from M5.2 also the GPU tier and lpe-editor on software OpenGL (Xvfb + Mesa llvmpipe), never counted as a hardware test. |
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
- CI has no GPU. From M5.2 the Linux jobs run the GPU tier on software OpenGL (Xvfb + Mesa llvmpipe); the hardware results (GPU tier, screenshots) come only from the local run and must be in the report, including the GPU and driver used, apart from the software-OpenGL results.
- A check that does not apply is reported as "not required" with the reason; a check that applies but could not run is reported as "not run", never as passed.
