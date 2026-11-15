# Third-party dependencies

Every dependency added to `conanfile.py` or vendored under `third_party/` is listed here (docs/TECH_STACK.md §10).

| Package | Version | License | Kind | Used by | Added in |
|---|---|---|---|---|---|
| Catch2 | 3.16.1 | BSL-1.0 | test-only | `tests/` | M0.1 |
| SDL3 (`sdl`) | 3.4.16, static | Zlib | runtime | `engine/src/platform/sdl3/` | M0.4 |
| GLAD 2 loader (vendored, generated) | glad2 2.0.8, `gl:core=4.5` | generated code: WTFPL or CC0-1.0; Khronos spec: Apache-2.0; `khrplatform.h`: Khronos MIT-style | runtime | `engine/src/render/opengl/` | M0.5 |
| GLM (`glm`) | 1.0.3, header-only | MIT or Happy Bunny (modified MIT), licensee's choice | runtime | `engine/core/Math.hpp` (public), camera, renderer (ADR 0007) | M1.5 |
| Dear ImGui (`imgui`) | 1.92.9b, static, default options | MIT | runtime | `engine/src/debug/DebugUi.cpp` and island-preview's WorldGen panel (`imgui.h`, public through the engine); the SDL3 and OpenGL 3 backends from the package's `res/bindings` are compiled in `third_party/imgui_backends/` and used only in `engine/src/platform/sdl3/Sdl3ImGui.cpp` and `engine/src/render/opengl/GlImGui.cpp` (ADR 0018) | M3.12 |
| EnTT (`entt`) | 4.0.0, header-only | MIT | runtime | `engine/src/scene/Scene.cpp` only (private): entity and component storage of `engine::scene::Scene` (ADR 0021) | M4.2 |
| Lua (`lua`) | 5.5.0, static, compiled as C++ (`compile_as_cpp=True`) | MIT | runtime | `engine/src/scripting/lua/` only (private): the script VM (ADR 0024) | M4.5 |
| nlohmann_json (`nlohmann_json`) | 3.12.0, header-only | MIT | runtime | `engine/src/core/JsonSupport.hpp` and the document sources `engine/src/worldgen/SettingsFile.cpp`, `engine/src/assets/AssetManifest.cpp`, `engine/src/scene/SceneDocument.cpp` only (private): world settings files (ADR 0004), the asset manifest (ADR 0022), scene documents (ADR 0023) | M3.11 |
| Raven (vendored, pruned) | 0.2.3 at commit `7ab270df3b618de3d11af6033912374b6cd621b7`, static C library `raven` | Apache-2.0, with its `NOTICE` (Copyright 2026 Oscar Soirey); bundled components below | runtime (the editor's UI from M5.2, ADR 0019); in M5.1 only the headless tests link it | `tests/unit/ui/` (M5.1); built from `third_party/raven/` by `cmake/RavenDependency.cmake` (ADR 0026). One local change (the HTTP(S) download compiled out), see `third_party/raven/README.md` | M5.1 |
| stb (`stb_image_write.h`, `stb_image.h` v2.30) | cci.20261021, header-only | MIT or public domain (Unlicense-style), licensee's choice | runtime | `engine/src/core/Image.cpp`: PNG encoding (ADR 0006) and PNG decoding (ADR 0008); implementations compiled in `third_party/stb_image_write/` and `third_party/stb_image/` | M1.2 (encoding), M2.4 (decoding) |

GLAD lives in `third_party/glad/` (settings in its `README.md`, licenses in its `LICENSE`); why it is not taken from Conan: ADR 0005.

### Components bundled in Raven

Raven compiles these into its library from `third_party/raven/third_party/` (licences checked against each file; details and the kept and removed paths in `third_party/raven/README.md`). Its own `LICENSE`, `NOTICE` and `THIRD_PARTY.md` are kept next to them.

| Component | Version | License (file in `third_party/raven/`) | In the `raven` library |
|---|---|---|---|
| stb_truetype (Sean Barrett) | 1.26, with two upstream Raven changes marked `Raven:` | MIT or public domain (Unlicense), licensee's choice (`third_party/stb/LICENSE`) | yes: font rasterization, `static` inside `src/render/font_raster.c` |
| stb_image (Sean Barrett) | 2.30 | MIT or public domain (Unlicense), licensee's choice (`third_party/stb/LICENSE`) | yes: image decoding, `static` inside `src/render/image_decode.c` (separate from the engine's stb_image) |
| nanosvg and nanosvgrast (Mikko Mononen) | upstream master of November 2026 | zlib (`third_party/nanosvg/LICENSE.txt`) | yes: SVG images, names prefixed `rvn_` in `src/render/svg_decode.c` |
| Roboto Regular, subset (Google) | subset made by Raven | Apache-2.0 (`third_party/roboto/LICENSE`) | yes: the default font, embedded in `src/resources/default_font.c` |
| Roboto Mono Regular, subset (The Roboto Mono Project Authors) | subset made by Raven | OFL-1.1 (`third_party/roboto-mono/LICENSE`) | yes: the fixed-width font, embedded in `src/resources/mono_font.c` |
| Wayland and wayland-protocols descriptions | as listed in `third_party/wayland-protocols/README.md` | MIT (`third_party/wayland-protocols/COPYING`) | no: the source of the generated `src/platform/wayland/wayland_protocols.h`, which is not compiled (`RAVEN_PLATFORM=null`) |

A program that ships Raven carries Raven's `LICENSE` and `NOTICE` and the licences of the compiled components above. Raven's designer fonts (in upstream's `tools/`) and Vulkan-Headers are not in the copy and not used.

SDL brings build-time tools from Conan Center: `cmake` (BSD-3-Clause) and, on Linux, `pkgconf` (ISC). On Linux it also uses system X11 and OpenGL packages (`xorg/system`, `opengl/system`); they are installed from the distribution, not redistributed. Only the video and OpenGL parts of SDL are enabled; see `conanfile.py`.

## Assets in the repository

Every committed asset that is not code (textures, models) is listed with its origin (`docs/milestones/m2-renderer-materials.md`, rules for M2). Assets made for the project carry no third-party rights.

| File | Origin | Added in |
|---|---|---|
| `assets/textures/crate.png` | Made for this project: 16×16 RGB pixel-art crate (frame, nails, planks, diagonal brace), its pixels set by a short script written during M2.7; no external source. | M2.7 |
| `assets/models/lighthouse.obj` | Made for this project: octagonal low-poly lighthouse (plinth, striped tower, gallery, lantern, roof), its vertices, UVs and normals computed by a short script written during M2.10; no external source. | M2.10 |
| `assets/textures/lighthouse.png` | Made for this project: 32×32 RGB texture atlas for the lighthouse (brick stripes with a window, lantern glass, roof, stone), its pixels set by the same script; no external source. | M2.10 |

## Developer tools (not linked, not shipped)

Pinned in `requirements-dev.txt`, installed from PyPI into `.venv` locally and in CI.

| Tool | Version | License | Platforms | Added in |
|---|---|---|---|---|
| Conan | 2.34.0 | MIT | Windows, Linux | M0.1 (pinned in M0.3) |
| clang-format (PyPI wheel of LLVM clang-format) | 22.1.3 | Apache-2.0 WITH LLVM-exception | Windows, Linux | M0.3 |
| Ninja | 1.13.2 | Apache-2.0 | Linux only | M0.3 |
| glad2 (generator, with Jinja2) | 2.0.8 | MIT | Windows, Linux | M0.5 — only to regenerate `third_party/glad` |

## Planned

Decided in ADRs, not yet added: none.
