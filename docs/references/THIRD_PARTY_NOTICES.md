# Third-party dependencies

Every dependency added to `conanfile.py` or vendored under `third_party/` is listed here (docs/TECH_STACK.md §10).

| Package | Version | License | Kind | Used by | Added in |
|---|---|---|---|---|---|
| Catch2 | 3.16.1 | BSL-1.0 | test-only | `tests/` | M0.1 |
| SDL3 (`sdl`) | 3.4.16, static | Zlib | runtime | `engine/src/platform/sdl3/` | M0.4 |
| GLAD 2 loader (vendored, generated) | glad2 2.0.8, `gl:core=4.5` | generated code: WTFPL or CC0-1.0; Khronos spec: Apache-2.0; `khrplatform.h`: Khronos MIT-style | runtime | `engine/src/render/opengl/` | M0.5 |
| GLM (`glm`) | 1.0.3, header-only | MIT or Happy Bunny (modified MIT), licensee's choice | runtime | `engine/core/Math.hpp` (public), camera, renderer (ADR 0007) | M1.5 |
| stb (`stb_image_write.h` only) | cci.20261021, header-only | MIT or public domain (Unlicense-style), licensee's choice | runtime | `engine/src/core/Image.cpp` (PNG encoding, ADR 0006); implementation compiled in `third_party/stb_image_write/` | M1.2 |

GLAD lives in `third_party/glad/` (settings in its `README.md`, licenses in its `LICENSE`); why it is not taken from Conan: ADR 0005.

SDL brings build-time tools from Conan Center: `cmake` (BSD-3-Clause) and, on Linux, `pkgconf` (ISC). On Linux it also uses system X11 and OpenGL packages (`xorg/system`, `opengl/system`); they are installed from the distribution, not redistributed. Only the video and OpenGL parts of SDL are enabled; see `conanfile.py`.

## Developer tools (not linked, not shipped)

Pinned in `requirements-dev.txt`, installed from PyPI into `.venv` locally and in CI.

| Tool | Version | License | Platforms | Added in |
|---|---|---|---|---|
| Conan | 2.34.0 | MIT | Windows, Linux | M0.1 (pinned in M0.3) |
| clang-format (PyPI wheel of LLVM clang-format) | 22.1.3 | Apache-2.0 WITH LLVM-exception | Windows, Linux | M0.3 |
| Ninja | 1.13.2 | Apache-2.0 | Linux only | M0.3 |
| glad2 (generator, with Jinja2) | 2.0.8 | MIT | Windows, Linux | M0.5 — only to regenerate `third_party/glad` |

## Planned

Decided in ADRs, not yet added: nlohmann_json 3.12.0 (MIT, ADR 0004).
