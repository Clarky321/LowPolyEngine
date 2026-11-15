# Raven 0.2.3 — vendored, pruned

This folder holds a pruned copy of [Raven](https://github.com/oscar-soirey/Raven), a retained-mode GUI library in C (Apache License 2.0), used as the UI library of the editor (ADR 0019). How it is packaged, configured and updated was decided in [ADR 0026](../../docs/adr/0026-raven-packaging.md). This README is the project's own; it replaces upstream's `README.md`, which is not part of the copy.

| | |
|---|---|
| Upstream | `https://github.com/oscar-soirey/Raven` |
| Release | Raven 0.2.3 (`project(raven VERSION 0.2.3)` in `CMakeLists.txt`, `RAVEN_VERSION_*` in `include/raven/raven_core.h`) |
| Commit | `7ab270df3b618de3d11af6033912374b6cd621b7` (2026-11-14, "docs: add Trendshift daily and weekly badges") |
| Validated by | `docs/experiments/raven-ui-evaluation.md`, `docs/experiments/raven-final-validation.md` (same commit) |
| Taken on | 2026-11-15, M5.1 |
| Pinned in | `cmake/RavenDependency.cmake` (`LPE_RAVEN_VERSION`, `LPE_RAVEN_COMMIT`); `tests/unit/ui/raven_null_backend_test.cpp` checks that the pin, Raven's own version and this README agree |
| CMake target | `raven`, a static C11 library (see *Build configuration*) |

Nothing is downloaded at configure or build time. This copy is the only Raven the build uses.

## How the copy was made

The copy is `git archive` of the pinned commit, restricted to the kept paths. Every one of the 249 upstream files is byte-identical to the commit except `src/core/fs.c` (*Local changes*). The commands, from an empty folder outside the repository:

```bash
git init raven-upstream && cd raven-upstream
git remote add origin https://github.com/oscar-soirey/Raven
git fetch --depth 1 origin 7ab270df3b618de3d11af6033912374b6cd621b7
git -c advice.detachedHead=false checkout FETCH_HEAD
git rev-parse HEAD        # must print the commit above
mkdir ../raven-pruned
git archive --format=tar HEAD CMakeLists.txt LICENSE NOTICE THIRD_PARTY.md cmake include src third_party \
  | tar -x -C ../raven-pruned
```

Check a checkout of this repository against it (only `README.md` and `src/core/fs.c` may differ):

```bash
git diff --no-index --stat ../raven-pruned <repository>/third_party/raven
```

## Kept paths

| Path | Files | Bytes | Why it is kept |
|---|---:|---:|---|
| `CMakeLists.txt` | 1 | 25 462 | Raven's own build: defines the target `raven` and its options. Used unchanged through `add_subdirectory` (`cmake/RavenDependency.cmake`). |
| `cmake/` | 5 | 5 722 | Included by `CMakeLists.txt`: `RavenCompilerOptions.cmake` (Raven's own warning level), `ravenConfig.cmake.in` (read by its install rules at configure time); `RavenVulkanHeaders.cmake`, `RavenShaders.cmake`, `RavenEmbedSpirv.cmake` are only read with Vulkan or shader rebuilding on, both off. |
| `include/` | 25 | 363 748 | The public C API (`Raven.h` and `raven/*.h`, every header `Raven.h` includes) and `raven/shaders/raven_effect.glsl`. |
| `src/` | 193 | 4 765 482 | The library. Kept whole, although each build compiles only part of it (*Build configuration*): the copy stays a plain subset of upstream that an update can compare file by file, Raven's CMakeLists.txt lists sources by platform, and the GLSL shaders in `src/render/vulkan/shaders/` are the source the project's OpenGL backend adapts (M5.2, ADR 0019). |
| `third_party/` | 22 | 1 036 374 | The components bundled into Raven, each with its licence (*Bundled components*). |
| `LICENSE` | 1 | 11 358 | Apache License 2.0, Raven's licence. |
| `NOTICE` | 1 | 194 | Raven's NOTICE file; Apache 2.0 section 4(d) requires it to be passed on. |
| `THIRD_PARTY.md` | 1 | 4 445 | Upstream's list of bundled components and their licences. It also lists the fonts of Raven's designer in `tools/designer/fonts/`; that folder is not in this copy. |
| **Total** | **249** | **6 212 785** | of 816 files and 36.0 MB at the commit |

Plus `README.md`, this file (the project's own).

<details>
<summary>Full tree of the kept files (249)</summary>

```text
third_party/raven/  (249 upstream files + README.md)
├── cmake/  (5)
│   ├── RavenCompilerOptions.cmake
│   ├── ravenConfig.cmake.in
│   ├── RavenEmbedSpirv.cmake
│   ├── RavenShaders.cmake
│   └── RavenVulkanHeaders.cmake
├── include/  (25)
│   ├── raven/  (24)
│   │   ├── shaders/  (1)
│   │   │   └── raven_effect.glsl
│   │   ├── raven_accessibility.h
│   │   ├── raven_android.h
│   │   ├── raven_backend.h
│   │   ├── raven_controls.h
│   │   ├── raven_core.h
│   │   ├── raven_dialogs.h
│   │   ├── raven_draw.h
│   │   ├── raven_emscripten.h
│   │   ├── raven_host.h
│   │   ├── raven_layout.h
│   │   ├── raven_locale.h
│   │   ├── raven_resources.h
│   │   ├── raven_shader.h
│   │   ├── raven_shell.h
│   │   ├── raven_style.h
│   │   ├── raven_text_editor.h
│   │   ├── raven_tools.h
│   │   ├── raven_touch.h
│   │   ├── raven_viewport.h
│   │   ├── raven_vulkan.h
│   │   ├── raven_widget.h
│   │   ├── raven_window.h
│   │   └── raven_xml.h
│   └── Raven.h
├── src/  (193)
│   ├── core/  (25)
│   │   ├── accessibility.c
│   │   ├── accessibility.h
│   │   ├── base.h
│   │   ├── context.c
│   │   ├── context.h
│   │   ├── error.c
│   │   ├── file.c
│   │   ├── fs.c
│   │   ├── fs.h
│   │   ├── fs_watch.c
│   │   ├── fs_watch.h
│   │   ├── handle.c
│   │   ├── handle.h
│   │   ├── heap.c
│   │   ├── heap.h
│   │   ├── jobs.c
│   │   ├── jobs.h
│   │   ├── lfmap.c
│   │   ├── lfmap.h
│   │   ├── locale.c
│   │   ├── memory.c
│   │   ├── thread.h
│   │   ├── thread_posix.c
│   │   ├── thread_win32.c
│   │   └── utf8.c
│   ├── platform/  (32)
│   │   ├── android/  (2)
│   │   │   ├── java/  (1)
│   │   │   │   └── raven/  (1)
│   │   │   │       └── android/  (1)
│   │   │   │           └── RavenTextView.java
│   │   │   └── android_platform.c
│   │   ├── cocoa/  (2)
│   │   │   ├── cocoa_platform.m
│   │   │   └── cocoa_print.m
│   │   ├── emscripten/  (3)
│   │   │   ├── emscripten_accessibility.c
│   │   │   ├── emscripten_platform.c
│   │   │   └── emscripten_services.c
│   │   ├── host/  (1)
│   │   │   └── host_platform.c
│   │   ├── null/  (1)
│   │   │   └── null_platform.c
│   │   ├── uikit/  (1)
│   │   │   └── uikit_platform.m
│   │   ├── wayland/  (2)
│   │   │   ├── wayland_platform.c
│   │   │   └── wayland_protocols.h
│   │   ├── win32/  (7)
│   │   │   ├── win32_manipulation.c
│   │   │   ├── win32_manipulation.h
│   │   │   ├── win32_platform.c
│   │   │   ├── win32_taskbar.c
│   │   │   ├── win32_taskbar.h
│   │   │   ├── win32_uia.c
│   │   │   └── win32_uia.h
│   │   ├── x11/  (4)
│   │   │   ├── x11_internal.h
│   │   │   ├── x11_keys.c
│   │   │   ├── x11_platform.c
│   │   │   └── x11_xi2.c
│   │   ├── atspi.c
│   │   ├── atspi.h
│   │   ├── notify.c
│   │   ├── platform.c
│   │   ├── platform.h
│   │   ├── sni.c
│   │   ├── sni.h
│   │   ├── taskbar.c
│   │   └── tray.c
│   ├── render/  (55)
│   │   ├── null/  (1)
│   │   │   └── null_backend.c
│   │   ├── vulkan/  (35)
│   │   │   ├── generated/  (12)
│   │   │   │   ├── blur_frag_spv.h
│   │   │   │   ├── blur_vert_spv.h
│   │   │   │   ├── effect_aurora_spv.h
│   │   │   │   ├── effect_grain_spv.h
│   │   │   │   ├── effect_mesh_spv.h
│   │   │   │   ├── effect_neon_spv.h
│   │   │   │   ├── effect_plasma_spv.h
│   │   │   │   ├── effect_ripple_spv.h
│   │   │   │   ├── effect_shimmer_spv.h
│   │   │   │   ├── effect_spotlight_spv.h
│   │   │   │   ├── ui_frag_spv.h
│   │   │   │   └── ui_vert_spv.h
│   │   │   ├── shaders/  (12)
│   │   │   │   ├── effects/  (8)
│   │   │   │   │   ├── aurora.frag
│   │   │   │   │   ├── grain.frag
│   │   │   │   │   ├── mesh.frag
│   │   │   │   │   ├── neon.frag
│   │   │   │   │   ├── plasma.frag
│   │   │   │   │   ├── ripple.frag
│   │   │   │   │   ├── shimmer.frag
│   │   │   │   │   └── spotlight.frag
│   │   │   │   ├── blur.frag
│   │   │   │   ├── blur.vert
│   │   │   │   ├── ui.frag
│   │   │   │   └── ui.vert
│   │   │   ├── vk_backend.c
│   │   │   ├── vk_backend.h
│   │   │   ├── vk_device.c
│   │   │   ├── vk_frame.c
│   │   │   ├── vk_glass.c
│   │   │   ├── vk_loader.c
│   │   │   ├── vk_loader.h
│   │   │   ├── vk_pipeline.c
│   │   │   ├── vk_public.c
│   │   │   ├── vk_surface.c
│   │   │   └── vk_texture.c
│   │   ├── webgl/  (4)
│   │   │   ├── effects.h
│   │   │   ├── generate_shaders.py
│   │   │   ├── shaders.h
│   │   │   └── webgl_backend.c
│   │   ├── bidi.c
│   │   ├── bidi.h
│   │   ├── device.c
│   │   ├── draw.c
│   │   ├── draw.h
│   │   ├── font.c
│   │   ├── font.h
│   │   ├── font_raster.c
│   │   ├── font_system.c
│   │   ├── image_decode.c
│   │   ├── renderer.c
│   │   ├── renderer.h
│   │   ├── shader.c
│   │   ├── svg_decode.c
│   │   └── texture.c
│   ├── resources/  (2)
│   │   ├── default_font.c
│   │   └── mono_font.c
│   ├── widget/  (15)
│   │   ├── actions.c
│   │   ├── anim.c
│   │   ├── direction.c
│   │   ├── events.c
│   │   ├── inspector.c
│   │   ├── layout.c
│   │   ├── look.c
│   │   ├── paint.c
│   │   ├── registry.c
│   │   ├── style.c
│   │   ├── theme.c
│   │   ├── touch.c
│   │   ├── widget.c
│   │   ├── widget.h
│   │   └── window.c
│   ├── widgets/  (55)
│   │   ├── background_blur.c
│   │   ├── block_editor.c
│   │   ├── border.c
│   │   ├── box_panel.c
│   │   ├── breadcrumb.c
│   │   ├── button.c
│   │   ├── canvas.c
│   │   ├── canvas_look.c
│   │   ├── checkbox.c
│   │   ├── color_picker.c
│   │   ├── combo_box.c
│   │   ├── common.c
│   │   ├── common.h
│   │   ├── date_picker.c
│   │   ├── dialogs.c
│   │   ├── dock_area.c
│   │   ├── expander.c
│   │   ├── file_browser.c
│   │   ├── grid.c
│   │   ├── header_bar.c
│   │   ├── image.c
│   │   ├── island.c
│   │   ├── label.c
│   │   ├── list_view.c
│   │   ├── markdown.c
│   │   ├── menu.c
│   │   ├── node_graph.c
│   │   ├── overlay.c
│   │   ├── pagination.c
│   │   ├── pdf_document.c
│   │   ├── pdf_print.c
│   │   ├── pdf_view.c
│   │   ├── platform_looks.c
│   │   ├── plot.c
│   │   ├── progress_bar.c
│   │   ├── rating.c
│   │   ├── scroll_box.c
│   │   ├── size_box.c
│   │   ├── slider.c
│   │   ├── spin_box.c
│   │   ├── splitter.c
│   │   ├── switcher.c
│   │   ├── syntax.c
│   │   ├── tab_view.c
│   │   ├── table.c
│   │   ├── text_editor.c
│   │   ├── text_input.c
│   │   ├── textedit.c
│   │   ├── tile_view.c
│   │   ├── time_picker.c
│   │   ├── timeline.c
│   │   ├── toast.c
│   │   ├── tree_view.c
│   │   ├── viewport.c
│   │   └── wrap_box.c
│   └── xml/  (9)
│       ├── xml.h
│       ├── xml_attributes.c
│       ├── xml_components.c
│       ├── xml_dom.c
│       ├── xml_loader.c
│       ├── xml_parser.c
│       ├── xml_schema.c
│       ├── xml_watch.c
│       └── xml_watch.h
├── third_party/  (22)
│   ├── nanosvg/  (3)
│   │   ├── LICENSE.txt
│   │   ├── nanosvg.h
│   │   └── nanosvgrast.h
│   ├── roboto-mono/  (2)
│   │   ├── LICENSE
│   │   └── RobotoMono-Regular-subset.ttf
│   ├── roboto/  (2)
│   │   ├── LICENSE
│   │   └── Roboto-Regular-subset.ttf
│   ├── stb/  (3)
│   │   ├── LICENSE
│   │   ├── stb_image.h
│   │   └── stb_truetype.h
│   └── wayland-protocols/  (12)
│       ├── COPYING
│       ├── cursor-shape-v1.xml
│       ├── ext-background-effect-v1.xml
│       ├── fractional-scale-v1.xml
│       ├── pointer-gestures-unstable-v1.xml
│       ├── README.md
│       ├── tablet-v2.xml
│       ├── viewporter.xml
│       ├── wayland.xml
│       ├── xdg-decoration-unstable-v1.xml
│       ├── xdg-shell.xml
│       └── xdg-toplevel-icon-v1.xml
├── CMakeLists.txt
├── LICENSE
├── NOTICE
└── THIRD_PARTY.md
```

Regenerate the list from a checkout: `git ls-files third_party/raven`.

</details>

## Removed paths

Everything at the commit that is not in *Kept paths*: 17 entries, 567 files, 29.8 MB. Nothing inside a kept path was removed. A removed folder is removed whole, with everything in it.

| Upstream path | Files | Bytes | Reason |
|---|---:|---:|---|
| `.gitattributes` | 1 | 655 | Git settings of Raven's repository. This repository's `.gitattributes` covers the copy (LF line endings, `*.ttf` binary) and exempts it from `git diff --check`, since upstream's whitespace is kept byte for byte. |
| `.github/` | 3 | 23 656 | Raven's own CI workflows. |
| `.gitignore` | 1 | 298 | Raven's repository ignores. |
| `CMakePresets.json` | 1 | 2 942 | Presets for building Raven alone; the project uses its own. |
| `README.md` | 1 | 77 470 | Raven's documentation for its users; replaced by this file. The API is documented in the headers in `include/`. |
| `ROADMAP.md` | 1 | 7 331 | Raven's plans; not code. |
| `bindings/` | 101 | 6 940 489 | Bindings for other languages (Python, Rust, C#, Go, Java, Zig, …). The project calls the C API from C++ directly. `RAVEN_BUILD_TESTS=OFF` keeps Raven's CMakeLists.txt from adding the folder. |
| `build-sdk-installer.bat` | 1 | 56 | Builds Raven's SDK installer; not used (`RAVEN_BUILD_INSTALLER=OFF`). |
| `build.bat` | 1 | 5 726 | Raven's own Windows build script; the project builds Raven through CMake. |
| `dist.bat` | 1 | 9 199 | Builds Raven's release packages. |
| `docs/` | 43 | 3 689 239 | Raven's website and guides (HTML, Markdown, images); not compiled. |
| `examples/` | 154 | 2 105 541 | Example programs (`RAVEN_BUILD_EXAMPLES=OFF`). |
| `generate-cmake.bat` | 1 | 4 913 | Generates Visual Studio solutions for Raven alone. |
| `openspec/` | 21 | 24 740 | Raven's design notes and change proposals; not code. |
| `tests/` | 45 | 738 805 | Raven's own tests and benchmark (`RAVEN_BUILD_TESTS=OFF`). The project tests Raven through its own headless tests in `tests/unit/ui/`. |
| `tools/` | 182 | 16 054 695 | The designer, the SDK installer and helper scripts (`RAVEN_BUILD_TOOLS=OFF`), with the designer's fonts (SIL OFL 1.1, listed in `THIRD_PARTY.md`): none of it is compiled into the library. |
| `web/` | 9 | 138 519 | Web pages driven in an installed Chromium browser (`raven_web.h`, `RAVEN_BUILD_WEB=OFF`). Raven's CMakeLists.txt still names `web/include` as a public include folder; the folder does not exist here, which CMake, MSVC and GCC accept, and no header of `include/` refers to it. |

## Local changes

One file of Raven is changed, `src/core/fs.c`. Every change is marked with a `LowPolyEngine:` comment, and nothing changes unless the macro `LPE_RAVEN_NO_HTTP_DOWNLOAD` is defined, which `cmake/RavenDependency.cmake` does for the target `raven`.

| File | Change | Why | Effect |
|---|---|---|---|
| `src/core/fs.c` | With `LPE_RAVEN_NO_HTTP_DOWNLOAD` defined, `rvn_fs_download_url` is a stub that returns `R_FALSE` (like upstream's own Emscripten stub), and `<winhttp.h>` is not included. | Upstream loads `http://` and `https://` image sources (XML `Source="…"` of Image, Viewport, a window background or theme wallpaper, and Markdown images) with WinHTTP on Windows and by starting a `curl` process on Linux. Raven has no build option to switch this off (ADR 0019 condition 5, ADR 0026). | Such a source fails like an unreadable file: Raven reports `R_ERROR_INVALID_FILE_FORMAT` "asynchronous texture: cannot decode <url>", and no request is made and no process started. `winhttp` stays on Raven's link line (its CMakeLists.txt adds it on every Windows build), but nothing references it, so programs do not import `winhttp.dll` (checked with `dumpbin /imports` and by `tests/unit/ui/raven_configuration_test.cpp`). |

The change against the commit (abridged: unchanged context lines are shortened; `git diff --no-index` against the pruned commit, see *How the copy was made*, shows it in full):

```diff
@@ -11,7 +11,10 @@
 #include <windows.h>
 #include <shellapi.h>
+/* LowPolyEngine: no WinHTTP without the HTTP(S) download (see rvn_fs_download_url). */
+#if !defined(LPE_RAVEN_NO_HTTP_DOWNLOAD)
 #include <winhttp.h>
+#endif
 #else
@@ -534,7 +537,20 @@ int rvn_is_http_url(const char* value) {
 #define RVN_FS_DOWNLOAD_MAX (32u * 1024u * 1024u)
-#if defined(_WIN32)
+#if defined(LPE_RAVEN_NO_HTTP_DOWNLOAD)
+/*
+ * LowPolyEngine: the HTTP(S) download is compiled out, on every system
+ * (third_party/raven/README.md, docs/adr/0026-raven-packaging.md). An image
+ * source "http://..." or "https://..." then fails like a file that cannot be
+ * read: no request, no WinHTTP, no curl process.
+ */
+int rvn_fs_download_url(const char* url, uint8_t** data, size_t* size) {
+	RVN_UNUSED(url);
+	if (data) *data = NULL;
+	if (size) *size = 0;
+	return R_FALSE;
+}
+#elif defined(_WIN32)
 int rvn_fs_download_url(const char* url, uint8_t** data, size_t* size) {
```

The changes upstream itself made to bundled code (two in `third_party/stb/stb_truetype.h`, marked `Raven:`, described in `THIRD_PARTY.md`) are part of the commit and untouched.

## Build configuration

`cmake/RavenDependency.cmake` sets Raven's options and adds this folder with `add_subdirectory(... SYSTEM)`. Raven's `CMakeLists.txt` is used as it is.

| Option | Value | Effect |
|---|---|---|
| `RAVEN_BUILD_SHARED` | OFF | `raven` is a static library (`raven.lib` / `libraven.a`). |
| `RAVEN_PLATFORM` | `null` | Only Raven's null platform (headless) and host platform (windows supplied by the application, `raven_host.h`) are compiled. No Win32, X11 or Wayland window code, no X11 headers or `libX11`. SDL3 owns the engine's windows (ADR 0002) and reaches Raven through the host platform (ADR 0019, M5.2). Set in the cache with `FORCE`: Raven declares it with `set(CACHE)`, which would drop a normal variable under its CMake 3.16 policies. |
| `RAVEN_WAYLAND` | OFF | No Wayland platform (it only exists beside X11). |
| `RAVEN_BACKEND_VULKAN` | OFF | No Vulkan backend: nothing looks for the Vulkan SDK or headers. The engine draws Raven with OpenGL 4.5 through a custom backend (M5.2). |
| `RAVEN_FETCH_VULKAN_HEADERS` | OFF | Never download (would use `FetchContent`). |
| `RAVEN_REBUILD_SHADERS` | OFF | No glslang; the SPIR-V headers stay as committed (and are not compiled without Vulkan). |
| `RAVEN_BACKEND_WEBGL` | OFF | Browser builds only. |
| `RAVEN_BUILD_WEB` | OFF | No `raven_web.h` web pages (they drive an installed browser). |
| `RAVEN_BUILD_EXAMPLES`, `RAVEN_BUILD_TESTS`, `RAVEN_BUILD_TOOLS`, `RAVEN_BUILD_INSTALLER` | OFF | Nothing but the library; `bindings/` is only added with the tests. |
| `RAVEN_WITH_STB_IMAGE` | ON | PNG/JPEG/BMP/TGA images in layouts (bundled stb_image). |
| `RAVEN_WITH_SVG` | ON | SVG images and icons (bundled nanosvg). |
| `RAVEN_WARNINGS_AS_ERRORS` | OFF | Raven's warnings never fail the project's build. |
| `RAVEN_MSVC_STATIC_RUNTIME` | OFF | The C runtime DLL, like the rest of the project. |
| `LPE_RAVEN_NO_HTTP_DOWNLOAD` (compile definition) | defined | The local change above. |

Configure prints `Raven 0.2.3: platform=null vulkan=OFF webgl=OFF shared=OFF`.

**Compiled:** `src/core/` (the thread file of the host system only), `src/platform/{platform,notify,taskbar,tray}.c`, `src/platform/null/`, `src/platform/host/`, `src/render/` without `vulkan/` and `webgl/`, `src/render/null/`, `src/resources/`, `src/widget/`, `src/widgets/`, `src/xml/`. **Not compiled:** `src/platform/{android,cocoa,emscripten,uikit,wayland,win32,x11}/`, `src/platform/atspi.c`, `src/platform/sni.c`, `src/render/vulkan/`, `src/render/webgl/`.

**Libraries Raven links:** Windows: `winhttp` (unreferenced, see *Local changes*); the Win32 system libraries its file dialogs, shell calls and looks use come from the default libraries CMake gives MSVC (`CMAKE_C_STANDARD_LIBRARIES`). `lpe_unit_tests.exe`, which links Raven, imports `KERNEL32`, `GDI32`, `ole32`, `SHELL32` and `COMDLG32` of Windows and nothing else (`dumpbin /imports`, Debug). Linux: `Threads::Threads`, `m`, `dl`. Nothing else, and nothing from Conan.

**Warnings.** Raven is compiled with its own warning level (`cmake/RavenCompilerOptions.cmake`: MSVC `/W4`, GCC `-Wall -Wextra -Wshadow -Wstrict-prototypes -Wpedantic`), never with the project's flags (`lpe_set_project_warnings` is not applied to it) and without warnings as errors. Its headers are SYSTEM includes for every target that links `raven`, so the project's `-Werror` code never sees warnings from them. At this commit MSVC 19.51 reports the same 9 warnings in Debug and Release: `src/core/file.c` C4310, `src/core/heap.c` C4389 ×2, `src/widget/style.c` C4702, `third_party/nanosvg/nanosvg.h` C4702, `src/widget/theme.c`, `src/widgets/pdf_view.c` and `src/widgets/text_editor.c` C4701, `src/widgets/combo_box.c` C4132. GCC 13.3 (CI, `ubuntu-24.04`) reports none in Debug and one in Release: `src/widget/theme.c:645` `-Wformat-truncation=`. They are upstream's and do not fail the build. An update compares these lists (GCC's from the CI log of the Linux jobs).

## Bundled components

What `third_party/` holds, checked against each file's own licence text:

| Component | Files | Licence (its file) | In the compiled library? |
|---|---|---|---|
| stb_truetype 1.26 (Sean Barrett) | `third_party/stb/stb_truetype.h` | MIT or public domain (Unlicense), the reader's choice — `third_party/stb/LICENSE` | yes: glyph outlines and rasterization (`src/render/font_raster.c`, as `static` functions) |
| stb_image 2.30 (Sean Barrett) | `third_party/stb/stb_image.h` | MIT or public domain (Unlicense) — `third_party/stb/LICENSE` | yes, `RAVEN_WITH_STB_IMAGE`: image decoding (`src/render/image_decode.c`, `static`; does not clash with the project's own stb_image) |
| nanosvg and nanosvgrast (Mikko Mononen) | `third_party/nanosvg/nanosvg.h`, `nanosvgrast.h` | zlib — `third_party/nanosvg/LICENSE.txt` | yes, `RAVEN_WITH_SVG`: SVG parsing and rasterization (`src/render/svg_decode.c`, names prefixed `rvn_`) |
| Roboto Regular, subset (Google) | `third_party/roboto/Roboto-Regular-subset.ttf`, embedded in `src/resources/default_font.c` | Apache License 2.0 — `third_party/roboto/LICENSE` | yes: the default font |
| Roboto Mono Regular, subset (The Roboto Mono Project Authors) | `third_party/roboto-mono/RobotoMono-Regular-subset.ttf`, embedded in `src/resources/mono_font.c` | SIL Open Font License 1.1 — `third_party/roboto-mono/LICENSE` | yes: the fixed-width font (`Rfont_get_monospace`) |
| Wayland and wayland-protocols protocol descriptions | `third_party/wayland-protocols/*.xml` | MIT (X.org "Expat" variant) — `third_party/wayland-protocols/COPYING` | no: they are the source of `src/platform/wayland/wayland_protocols.h`, which is kept in `src/` but not compiled (`RAVEN_PLATFORM=null`). Kept so that the copy's generated header keeps its source and licence. |

The fonts are subsets (modified versions under Apache 2.0 and OFL 1.1), as upstream's `THIRD_PARTY.md` states. Vulkan-Headers, which upstream needs for its Vulkan backend, is not used and not in the copy.

Notices kept: `LICENSE`, `NOTICE`, `THIRD_PARTY.md`, `third_party/stb/LICENSE`, `third_party/nanosvg/LICENSE.txt`, `third_party/roboto/LICENSE`, `third_party/roboto-mono/LICENSE`, `third_party/wayland-protocols/COPYING` (and its `README.md`). The project lists them in `docs/references/THIRD_PARTY_NOTICES.md`. A program that ships Raven must carry Raven's `LICENSE` and `NOTICE` and the licences of the compiled components above.

## Run-time features reviewed (ADR 0019 condition 5)

From the sources at this commit; ADR 0026 records the decisions, `tests/unit/ui/raven_configuration_test.cpp` checks them.

- **Live reload** of XML and theme files: on by default. `Rxml_set_live_reload(R_FALSE)` after `Rinit` (or `RAVEN_LIVE_RELOAD=0` in the environment) turns it off: file changes are then ignored. What remains: a window or theme created from a *file* still registers an OS file watch (a watcher thread, `src/core/fs_watch.c`) whose events are dropped, and `Rxml_check_files()` still reloads changed files when called. Text given from memory (`Rwindow_create_from_xml`) is never watched.
- **Search paths and auto-loading:** on by default. `Rxml_set_auto_load(R_FALSE)` after `Rinit` and before the first document (or `RAVEN_AUTO_LOAD=0`) stops Raven from loading translation tables (`*.csv`) and widget declarations (`*.xml` with a `<Widgets>` root, and `<Tag>.xml` for an unknown tag) from the document's folder and its subfolders, the base path, the current folder and every folder of `PATH`. What remains, and cannot be switched off: a *relative* path written in a document (`Source=`, `Font=`, `<Include>`, `Theme=`) that does not name a file next to the document (or in the base path, for memory documents) is looked for in the document's subfolders, the base path, the current folder and the `PATH` folders. An absolute path, a file next to the document and a file registered with `Rfile_register_memory` are used directly.
- **HTTP(S) image loader:** compiled out by the local change above.

## Updating Raven

A Raven update is a change of its own: one reviewable commit (or pull request) that does nothing else, never part of another step (M5 plan, decision 1).

1. **Choose the commit.** A tagged release or a specific commit of `https://github.com/oscar-soirey/Raven`, never a branch name. Read upstream's changes since `7ab270df3b618de3d11af6033912374b6cd621b7` (`git log --oneline <old>..<new>`), especially `include/` (API) and `CMakeLists.txt` (options, sources, links).
2. **Fetch it** into an empty folder outside the repository, as in *How the copy was made*, with the new commit. Check `git rev-parse HEAD`.
3. **Prune it** with the same `git archive` command into a new empty folder. If upstream added a top-level path, decide whether it is kept, and record it in the tables above.
4. **Compare** the new pruned tree with this folder: `git diff --no-index --stat third_party/raven <new-pruned>`, then the full diff of `include/`, `CMakeLists.txt`, `cmake/`, `third_party/` and the `src/` files the project uses (the host and null platforms, `src/core/fs.c`, the XML loader, the widgets of the editor). Look for new options, new link libraries, new downloads (`FetchContent`, `file(DOWNLOAD)`, `ExternalProject`), new network or process calls (`grep -rn "WinHttp\|curl\|exec\|socket\|http" src`).
5. **Check the licences:** `LICENSE`, `NOTICE`, `THIRD_PARTY.md` and every licence file under `third_party/`. A new or changed bundled component needs its licence read and its row in *Bundled components* and in `docs/references/THIRD_PARTY_NOTICES.md`.
6. **Re-apply and check the local changes.** Replace this folder's contents with the new pruned tree (keeping `README.md`), then re-apply the `src/core/fs.c` change (the diff above; adapt it if the HTTP code moved). Verify that `git diff --no-index` against the new pruned tree shows only `README.md` and the documented changes, and that no other WinHTTP or `curl` path appeared. If upstream now has a build option for the download, prefer it and drop the source change.
7. **Update the pin:** `LPE_RAVEN_VERSION` and `LPE_RAVEN_COMMIT` in `cmake/RavenDependency.cmake`, `kReviewedVersion` and `kReviewedCommit` in `tests/unit/ui/raven_null_backend_test.cpp`, and the commit and version in this README.
8. **Build Debug and Release from clean build folders** on Windows (`scripts\windows\clean.bat`, then `scripts\windows\build.bat debug` and `release` with `LPE_WARNINGS_AS_ERRORS=ON`); Linux Debug and Release run in CI. Compare Raven's own warnings with the list under *Build configuration*. Check that configure prints `platform=null vulkan=OFF` and downloads nothing.
9. **Run the tests:** `scripts\windows\test.bat debug` and `release` (the headless tier, with `tests/unit/ui/`), the GPU tier from M5.2 on (`scripts\windows\test.bat <config> gpu`, the editor's runs), and `scripts\windows\format-check.bat`. On Windows, check the imports: `dumpbin /imports build\windows-msvc-debug\bin\Debug\lpe_unit_tests.exe` must not list `WINHTTP.dll`.
10. **Update the documents:** this README (tables, tree, sizes, warnings, the review of run-time features), `docs/references/THIRD_PARTY_NOTICES.md`, ADR 0026 (and ADR 0019 if the integration is affected), `docs/TECH_STACK.md`.
11. **Commit** as one change, e.g. `build(raven): update Raven to <version> (<commit>)`, with the checks in the body; push and wait for CI on all four jobs.
