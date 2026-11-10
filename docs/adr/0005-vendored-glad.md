# ADR 0005: Vendor the generated GLAD loader instead of using the Conan package

- **Status:** Accepted
- **Date:** 2026-11-09
- **Related:** ADR 0001, ADR 0002, `docs/TECH_STACK.md` §4 and §6

## Context

The OpenGL 4.5 renderer needs a function loader. GLAD 2 was chosen in ADR 0001. AGENTS.md prefers Conan for third-party code when it is reproducibly available; `glad/2.0.8` exists in Conan Center.

## Problem

The Conan recipe does not ship generated sources: it runs the glad2 Python generator (with Jinja2) while building the package. It finds Python through CMake's `FindPython`, which may pick any interpreter on the machine (on Windows the registry is searched first). Only Conan Center's own build service gets an explicit interpreter. A machine whose first-found Python lacks Jinja2 cannot build the package, and the result depends on which Python is found. Conan Center has no prebuilt binary for our MSVC 19.5x profile, so every developer and CI job would build it.

## Options

1. **Conan `glad/2.0.8`** — standard workflow, but build-time Python discovery is not reproducible (see above). Working around it means passing a machine-specific interpreter path through Conan configuration.
2. **Vendor generated code** — generate once with a pinned generator, commit the output with metadata. No Python at build time; the code is plain C. `docs/TECH_STACK.md` §6 already allows this for generated loader code.
3. **Another loader (e.g. glbinding, glew)** — new dependency and API for no gain.

## Decision

Option 2. `third_party/glad/` contains the output of glad2 2.0.8 for `gl:core=4.5`, no extensions, no internal loader (SDL3 supplies `SDL_GL_GetProcAddress`). `scripts/python/generate_glad.py` regenerates it with `--reproducible` (bundled Khronos XML, no download); glad2 is pinned in `requirements-dev.txt`. `third_party/glad/README.md` records the settings, `LICENSE` the licenses.

## Consequences

- No Python or network access is needed to build the engine.
- Updating GLAD or the GL version is an explicit, reviewable change: bump the pin, run the script, commit.
- The generated code is excluded from formatting (`third_party/.clang-format` disables it) and compiled without project warning flags.
- GLAD keeps OpenGL function pointers in global variables. This matches the current design (one context, one render thread); multiple contexts would need the `--mx` variant.
- Revisit if the Conan recipe starts shipping pre-generated sources or the project needs several GL contexts.
