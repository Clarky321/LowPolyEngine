# ADR 0006: Frame capture to PNG with stb_image_write

- **Status:** Accepted
- **Date:** 2026-11-10
- **Related:** `docs/TESTING_AND_CI.md` §1 (rendering tier), `docs/TECH_STACK.md` §4

## Context

From M1 on, every visually significant step is verified with a real screenshot stored in `docs/screenshots/`. The GPU test tier also needs to read rendered pixels back to check what was drawn. M0 had no capture path. Screenshots go into Git, so they must be compressed: a 1280×720 RGB image is 2.7 MB uncompressed, while flat-shaded low-poly frames compress to a few hundred KB or less.

## Problem

The C++ standard library cannot encode images. The options are a small dependency, a self-written encoder, or an external conversion step.

## Options

| Option | Notes |
|---|---|
| `stb/cci.20261021` (Conan Center), `stb_image_write.h` | Header-only, MIT or public domain (choice), all platforms. Compressed PNG with its own deflate. `stb_image.h` from the same package is the planned texture loader for M2. |
| Own PNG writer with uncompressed ("stored") deflate blocks | ~100 lines, no dependency, but files stay 2.7 MB each. |
| Own deflate compressor | Several hundred lines of compression code to write and test. Not the engine's job. |
| Write raw/PPM and convert with a Python script | Two steps and two tools for one task; Python must not be a runtime requirement. |

## Decision

Use `stb_image_write` from the Conan package `stb/cci.20261021`.

- The implementation is compiled once in its own target, `third_party/stb_image_write/` (a one-line C file), without project warning flags, like GLAD (ADR 0005). Compiling it inside a project source file failed the Linux GCC 13 Release build: `-Wnull-dereference` reports stb's unchecked `realloc` once `-O3` inlines it, even though the header is a system include. Only `engine/src/core/Image.cpp` includes the header, for declarations. `STBI_WRITE_NO_STDIO` is set on the target: encoding goes to memory, and `core::writeBinaryFile` writes the file, so Unicode paths and I/O errors are handled by project code.
- The public API is project-owned: `core::Image` (RGB8, top row first), `core::encodePng`, `core::writePng`. No stb type appears in a header.
- The renderer reads the back buffer with `Renderer::captureFrame()`; the application loop writes it to a PNG on request (`--screenshot PATH`, F12). This is the single capture mechanism for screenshots and GPU tests.
- stb headers are external includes for project code, and the implementation is outside project targets, so project warning flags never apply to stb code.

## Consequences

- One header-only dependency, recorded in `docs/references/THIRD_PARTY_NOTICES.md`.
- Captures contain only the rendered framebuffer, not the OS window frame. That is the exact output of the renderer and reproducible.
- When M2 adds texture loading, `stb_image.h` comes from the same package; that use gets its own review (untrusted input, AGENTS.md §5).
- Revisit if PNG encoding speed matters (e.g. video capture) or a different image library is adopted for the asset pipeline.
