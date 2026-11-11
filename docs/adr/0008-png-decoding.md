# ADR 0008: PNG decoding with stb_image, treating files as untrusted

- **Status:** Accepted
- **Date:** 2026-11-10
- **Related:** ADR 0006 (PNG encoding with stb_image_write), `docs/milestones/m2-renderer-materials.md` M2.4, AGENTS.md §5 (imported assets are untrusted)

## Context

M2 needs textures (M2.5, M2.7), and textures come from image files. Those files may come from outside the project: downloaded asset packs, files from other people, later user content. A decoder that crashes, hangs or allocates gigabytes on a broken or hostile file would take the engine or editor down. `core::Image` (ADR 0006) only held RGB8 frame captures; textures also need alpha.

## Options

| Option | Notes |
|---|---|
| `stb_image.h` from `stb/cci.20261021` (already a dependency, ADR 0006) | v2.30, MIT or public domain, single header, all platforms. Decodes all PNG color types and bit depths, interlacing included. Can be limited to PNG. Not hardened for hostile input; memory-safety bugs in malformed files have been reported against stb_image in the past. |
| libpng / libspng (Conan) | Reference or security-focused decoders with zlib. A new dependency (plus zlib) for one format. |
| lodepng | Single file, careful decoder, but a second image library next to stb. |
| Own decoder | PNG plus inflate is far more code than the engine should own and test. |

## Decision

Decode PNG with `stb_image` from the existing `stb` package, behind a project-owned API, and put the project's own limits in front of it.

- **API** (`engine/core/Image.hpp`): `decodePng(std::span<const std::uint8_t>)` and `readPng(path)` return `core::Result<core::Image>`. No stb type appears in a header.
- **Image format:** `core::Image` gains `PixelFormat format` (`Rgb8`, the default, or `Rgba8`), placed last so `Image{width, height, pixels}` stays the RGB frame capture. `validate()` and `encodePng()` handle both. Decoding always yields RGBA8, top row first: gray and palette colors are expanded, missing alpha becomes 255, 16-bit channels keep their high byte (stb's conversion).
- **Formats:** PNG only (`STBI_ONLY_PNG`). Other image formats are not accepted until a concrete need appears; each one would be more untrusted parsing code.
- **Limits, checked before decoding:**
  - data and files up to `kMaxPngFileBytes` (256 MiB); `readPng` checks the file size before it reads the file;
  - the PNG signature is checked by project code, so the error says "not a PNG file";
  - the header is read with `stbi_info_from_memory` (no pixel decoding, no image allocation); width and height must be in 1..`kMaxImageExtent` (16384) and width × height at most `kMaxPngPixels` (64 Mi pixels, 256 MiB as RGBA). The pixel limit stops a tiny, highly compressed file from making the decoder allocate gigabytes.
  - Errors are project messages with stb's short reason; `readPng` names the file. An allocation failure while copying the pixels becomes an error, not an exception.
- **Build:** the implementation is compiled once in `third_party/stb_image/`, without project warning flags, like `stb_image_write` (ADR 0006). `STBI_ONLY_PNG`, `STBI_NO_STDIO` (files are read by `core::readTextFile`) and `STBI_NO_LINEAR` are set for the implementation and every includer. Only `engine/src/core/Image.cpp` includes the header.
- **Global state:** the project never changes stb's process-wide settings (vertical flip, unpremultiply, iPhone PNG conversion). stb keeps its failure reason per thread.

## Threat model

- **In scope:** PNG files from outside the project, read on a desktop machine by the engine or editor. Goals: no crash, no hang, no unbounded allocation, a clear error.
- **Mitigations:** the limits above, the header check before decoding, and tests that flip every byte of valid files and cut them at every length (`tests/unit/core/png_decode_test.cpp`); every result must be an error or a valid image within the limits.
- **Residual risks:** stb_image is not a hardened decoder. Within the limits a malicious file could still hit an unknown bug in it; there is no sandboxing. A palette index beyond the file's palette makes stb read unset entries of its own 1024-byte palette buffer (garbage colors, no out-of-bounds access). Decoding time for allowed sizes is not bounded beyond the pixel limit.
- **Revisit** when the engine loads content that is meant to be shared publicly, when fuzzing in CI is added (`docs/TESTING_AND_CI.md` §3), or if a hardened decoder (libspng) becomes worth a dependency.

## Consequences

- No new package: `stb_image.h` comes from the `stb` package already used for encoding; `docs/references/THIRD_PARTY_NOTICES.md` lists both headers.
- `core::Image` has a format field; frame captures and their PNG files are unchanged.
- Textures (M2.5) get RGBA8 images from `readPng`; mapping the top-to-bottom rows to UV (0, 0) at the bottom left (ADR 0007, Amendment 1) is the texture upload's job.
