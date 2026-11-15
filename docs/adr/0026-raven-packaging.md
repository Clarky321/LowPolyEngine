# ADR 0026: Raven packaging

- **Status:** Accepted (2026-11-15, M5.1). The packaging itself, pruned vendored source, is decision 1 of the M5 plan, approved by the user on 2026-11-15. The details below were decided in M5.1 (`docs/DEVELOPMENT_WORKFLOW.md` §4, "Decisions the plan leaves open").
- **Related:** ADR 0019 (Raven for the editor; conditions 2 and 5), ADR 0002 (SDL3 owns the windows), ADR 0005 (vendored GLAD), `docs/milestones/m5-minimal-editor.md` (M5.1, decision 1), `third_party/raven/README.md` (the lists, the local change, the update procedure), `cmake/RavenDependency.cmake`, `docs/TECH_STACK.md` §4–§6, AGENTS.md §3 and §7

## Context

ADR 0019 made Raven 0.2.3 at commit `7ab270df3b618de3d11af6033912374b6cd621b7` the editor's UI library. Its condition 2 asks for pinned source, compiled without the project's warning flags, with Vulkan, web pages, examples, tests and tools off, and updated only by a reviewed commit. Condition 5 asks for a review of three run-time features: live reload of XML and theme files, auto-loading from search paths, and the HTTP(S) image loader. AGENTS.md §7 forbids downloads during configure or build, and a clean checkout must build with the documented commands.

What Raven is like at that commit:

- It is not in Conan Center.
- Its repository is 816 files and 36 MB. Most of it is bindings for other languages, the designer and other tools, documentation and examples.
- Its `CMakeLists.txt` builds a static C11 library. It can download the Vulkan headers with `FetchContent`.
- It links `winhttp` on every Windows build.

## Options

1. **Pruned vendored source in `third_party/raven/`.** The library's own paths are copied from the pinned commit. The build is offline, and a local change sits in the repository where review sees it. Updating it is a manual, documented procedure.
2. **A project Conan recipe that fetches the commit.** The package would come from Conan like the rest. But `conan install` would download from GitHub, the project would maintain a recipe and carry its patch there, and the source would sit in a cache instead of the repository.
3. **A Git submodule.** This pins the commit, but it brings the whole 36 MB tree and needs an extra checkout step. A local change would need a fork.
4. **`FetchContent`.** This downloads at configure time, which AGENTS.md §7 forbids.

The plan chose option 1 (decision 1).

## Decision

1. **What is vendored.**
   - Kept: `CMakeLists.txt`, `LICENSE`, `NOTICE`, `THIRD_PARTY.md`, `cmake/`, `include/`, `src/` and `third_party/` of the pinned commit, taken with `git archive`. That is 249 files and 6.2 MB.
   - Removed: everything else, 17 entries and 567 files. These are `bindings/`, `docs/`, `examples/`, `openspec/`, `tests/`, `tools/`, `web/`, `.github/`, the root scripts and presets, `ROADMAP.md`, the repository's Git files and upstream's `README.md`.
   - Nothing inside a kept path is removed. `src/` stays whole, so the copy is a plain subset of upstream: an update compares it file by file, and the M5.2 OpenGL backend adapts the GLSL sources in `src/render/vulkan/shaders/`.
   - `third_party/raven/README.md`, the project's own, lists every kept and removed path with its reason and size, the full tree, and the commands to reproduce the copy.
2. **One local change, in `src/core/fs.c`.** The HTTP(S) download is compiled out when `LPE_RAVEN_NO_HTTP_DOWNLOAD` is defined, and `cmake/RavenDependency.cmake` defines it (decision 5). The change is marked `LowPolyEngine:`. Every other file is byte-identical to the commit. Raven's `CMakeLists.txt` is not changed: the configuration is set from outside.
3. **The target `raven`.**
   - `cmake/RavenDependency.cmake`, included from the root `CMakeLists.txt`, sets Raven's options and adds the folder with `add_subdirectory(... SYSTEM)`. The result is Raven's own static C11 target `raven`.
   - Off: Vulkan (with `RAVEN_FETCH_VULKAN_HEADERS`), WebGL, the web pages, examples, tests, tools, the installer, the shared library, shader rebuilding and Wayland.
   - On: stb_image and nanosvg (images and SVG icons in layouts).
   - Raven keeps its own warning level and builds without warnings as errors. It never gets `lpe_set_project_warnings`. Its headers are SYSTEM includes for every target that links it, so the project's `-Werror` code sees no Raven warning.
   - The language C was already enabled in `project()` for GLAD, so nothing changes there. Nothing is downloaded, and Conan and the lockfile are unchanged.
   - The pin, `LPE_RAVEN_VERSION` and `LPE_RAVEN_COMMIT`, is set in the same file. A unit test checks it against `Rget_version()`, `raven_core.h` and the vendored README.
4. **Window system: `RAVEN_PLATFORM=null`.** Only Raven's null and host platforms are compiled. Why:
   - **What Raven would otherwise build.** Upstream's `auto` builds the Win32 platform on Windows. On Linux it builds the X11 platform: `find_package(X11 REQUIRED)` (the X11 headers and `libX11`), plus AT-SPI accessibility and a tray icon, which open libdbus at run time. With `RAVEN_WAYLAND` on, which is upstream's default, it also builds the Wayland platform; that opens `libwayland-client`, `libxkbcommon` and `libdecor` at run time and needs nothing to build.
   - **Why none of it is needed.** The engine's windows belong to SDL3 (ADR 0002). Raven reaches them through its host platform (ADR 0019; the bridge comes in M5.2). Raven always picks the host platform when one is registered (`src/platform/platform.c`), so the prototype and the final validation never ran the native platforms, although they compiled them (`auto` with Wayland off).
   - **What `null` changes.** No native window code, no X11 dependency for Raven on Linux, and no Wayland. A Raven started without a host platform stays headless instead of opening an OS window behind SDL3's back; a unit test checks this.
   - **What the host platform does not offer** with either setting: OS windows for torn-off panels (ADR 0019 condition 4), and OS accessibility (UI Automation, AT-SPI), which lives in the native platforms.
   - **What does not depend on the setting.** File dialogs (`src/widgets/dialogs.c`) are compiled either way, and M5.11 checks them.
   - **Reopening.** If a later step needs a native platform, it changes this option in a reviewed commit.
5. **The HTTP(S) image loader is compiled out.**
   - **What upstream does.** An `http://` or `https://` image source (`Source=` of Image or Viewport, a window background, a theme wallpaper, a Markdown image) is downloaded on a worker thread. On Windows that is WinHTTP; on Linux, a `curl` process found through `PATH`.
   - **Why a source change.** Raven has no build option for this.
   - **What the change does.** The download is a stub that fails, as upstream's Emscripten build already has, and `<winhttp.h>` is not included. Such a source then fails like an unreadable file: Raven reports `R_ERROR_INVALID_FILE_FORMAT` "asynchronous texture: cannot decode <url>". No request is made and no process is started.
   - **What stays.** `winhttp` stays on Raven's link line, but nothing references it, so programs do not import `winhttp.dll`.
   - **How it is checked.** `dumpbin /imports` shows no `winhttp.dll`. A unit test checks that `winhttp.dll` is not loaded on Windows, and on Linux that a `curl` placed first in `PATH` is never run. With the definition removed, the Windows check fails (tried in M5.1).
   - **Reopening.** If a later Raven gets an option for this, the update uses it and drops the source change.
6. **Live reload and auto-loading are run-time switches, turned off.**
   - **Live reload.** It is on by default. `Rxml_set_live_reload(R_FALSE)` turns it off, and `RAVEN_LIVE_RELOAD=0` does the same from the environment. Called after `Rinit`, the call wins over the environment.
     - What remains when it is off: a window or theme created from a *file* still registers an OS file watch, whose events are dropped, and `Rxml_check_files()` still reloads changed files when the program calls it. Text given from memory is never watched.
   - **Auto-loading.** It is on by default. `Rxml_set_auto_load(R_FALSE)`, called after `Rinit` and before the first document, turns it off; `RAVEN_AUTO_LOAD=0` does the same.
     - What it would load: translation tables (`*.csv`) and widget declarations (`<Widgets>` files, and `<Tag>.xml` for an unknown tag).
     - Where from: the document's folder and its subfolders, the base path, the current folder and every folder of `PATH`.
     - What remains when it is off: a *relative* reference that does not name an existing file next to the document (or in the base path, for memory documents) is still looked for in the document's subfolders, the base path, the current folder and `PATH`. This cannot be switched off.
   - **The rule for every Raven session in the project.** Turn both off right after `Rinit`, before any document. The headless tests do so from M5.1, and the engine's Raven owner does from M5.2.
   - **Predictable loading.** Code that loads editor documents reads them itself and gives Raven the text: no watch, no auto-loading. Every file a document references must exist where the document's folder or the base path names it, or be registered with `Rfile_register_memory`, so the search fallback is never reached. M5.2 decides the details in its own ADR.
   - **How it is checked.** Unit tests show that with live reload off a changed file is not reloaded until `Rxml_check_files()` is called; with live reload switched on, the same check fails, which was tried in M5.1. They also show that with auto-loading off a declaration file in the base path is not taken, while registering it explicitly works.
7. **Other run-time behaviour observed, left as it is.** The editor uses none of these:
   - `Ropen_url`: the `open:` actions and links in text open a browser through `ShellExecute` or `xdg-open`.
   - PdfView opens the PDFium library at run time when a PDF is shown.
   - Raven also reads `RAVEN_ALLOCATOR` (`system` hands its memory to `malloc`, which makes Raven's own allocation count useless for leak checks), `RAVEN_APP_ID` and `RAVEN_PDFIUM`.
8. **Headless tests** (`tests/unit/ui/`, in `lpe_unit_tests`).
   - Raven starts with `R_BACKEND_NULL` and a test host platform, with no render thread and no worker threads.
   - The tests cover:
     - a window from XML text, a click and typed text (ASCII and Cyrillic) through `Rhost_inject_*`, and a button callback that changes a label;
     - failed starts, broken XML, repeated start and stop;
     - the pin, and the switches and the local change above;
     - `allocation_count == 0` and an intact heap after every `Rshutdown`.
   - Raven's state is per process, so there is one Raven at a time and never two.
9. **Updates** are a change of their own, never part of another step. The procedure is in `third_party/raven/README.md`, "Updating Raven":
   - fetch the new commit, prune it with the same command, and compare;
   - check the licences;
   - re-apply and check the local change;
   - update the pin;
   - build Debug and Release from clean folders, and run both test tiers and the format check;
   - update the README, the notices, this ADR and TECH_STACK, and commit.
10. **Notices.** `LICENSE` (Apache 2.0), `NOTICE` and `THIRD_PARTY.md` are kept, with the licence of every bundled component:
    - stb_truetype and stb_image: MIT or Unlicense;
    - nanosvg: zlib;
    - Roboto: Apache 2.0;
    - Roboto Mono: OFL 1.1;
    - the Wayland protocol files: MIT (not compiled).

    `docs/references/THIRD_PARTY_NOTICES.md` lists them.

## Consequences

- Configure and build need no network access. A clean checkout has Raven, about 6.2 MB of C.
- The build compiles about 110 more C files. Raven's own warnings (9 with MSVC, upstream's) appear in build logs and never fail the build.
- Raven's `CMakeLists.txt` names `web/include` as a public include folder, and that folder is not vendored. CMake and the compilers accept the missing folder.
- On Windows, linking `raven` still names `winhttp.lib`, which is inert.
- The project owns one small source change and must re-apply it on every update. The README's diff and the HTTP test make a missed re-application visible.
- No Raven header reaches the engine yet: only `tests/unit/ui/` links `raven` in M5.1. M5.2 decides where the engine may include it (ADR 0019, M5 plan).
- **Revisit:**
  - if Raven appears in Conan Center with a suitable recipe;
  - if upstream adds a switch for the download, or a way to switch off the search fallback;
  - if a step needs one of Raven's native platforms.
