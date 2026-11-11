# ADR 0009: GPU textures behind handles; display-space colors in M2

- **Status:** Accepted
- **Date:** 2026-11-10
- **Related:** ADR 0007 (mesh handles, UV convention), ADR 0008 (PNG decoding), `docs/milestones/m2-renderer-materials.md` M2.5, AGENTS.md §3 and §6

## Context

M2.7 draws textured materials, so the renderer needs GPU textures first. Two questions had to be answered: how engine code refers to textures and controls sampling, and in which color space colors, textures and (from M2.8) lighting are computed. Until now the renderer writes vertex colors unchanged into a framebuffer without sRGB conversion (`GL_FRAMEBUFFER_SRGB` is never enabled, the SDL context does not request an sRGB-capable framebuffer).

## Decision

### Texture API

- `Renderer::createTexture(const core::Image&, const TextureDesc& = {}) → core::Result<TextureHandle>`, `destroyTexture`, `isValid(TextureHandle)`. `TextureHandle` is `{index, generation}`, stored in the same `detail::HandlePool` as meshes, so null and stale handles are detected the same way. Destroying the renderer releases all textures. Handles are never serialized.
- `TextureDesc`: `filter` (`Nearest` or `Linear`), `wrap` (`Repeat` or `ClampToEdge`, both axes), `mipmaps` (on or off). Defaults: `Linear`, `Repeat`, mipmaps on — suitable for color textures on 3D surfaces; pixel-art looks choose `Nearest`.
- No OpenGL type in `Renderer.hpp`. Images that fail `core::validate()` are rejected with the reason; there is no texture update yet (added when something needs it).

### OpenGL backend (`GlTexture`)

- Immutable storage (`glTextureStorage2D`) in `GL_RGB8` or `GL_RGBA8`, from `core::Image` RGB8 or RGBA8. OpenGL 4.5 guarantees `GL_MAX_TEXTURE_SIZE >= 16384 = core::kMaxImageExtent`, so every valid image fits.
- `core::Image` rows run top to bottom; UV (0, 0) is the image's bottom-left corner (ADR 0007, Amendment 1). The upload writes the rows bottom up, one row per call, so texture row 0 (V = 0) is the image's bottom row. No flipped copy of the image is made.
- With mipmaps, the full chain down to 1×1 is allocated and generated with `glGenerateTextureMipmap`. Minification uses `LINEAR_MIPMAP_LINEAR` or `NEAREST_MIPMAP_NEAREST` with mipmaps, `LINEAR` or `NEAREST` without.
- Sampling state lives on the texture object. Separate sampler objects are added only when one texture must be sampled in two ways.

### Color space in M2: display space

All colors stay display-referred (sRGB-encoded values used as they are), as in M1:

- textures are stored as plain `UNORM` formats, without sRGB decoding (`GL_RGB8`, not `GL_SRGB8`);
- vertex colors and the material colors of M2.6 are the same kind of value;
- lighting in M2.8 multiplies these values directly;
- the framebuffer has no sRGB conversion, and frame captures read the values back unchanged.

Moving to linear lighting now would change the context setup (sRGB-capable framebuffer), every vertex color of the test scene, alpha blending of the overlay, and the expected values of the M1 GPU tests. That is a cross-cutting change of its own, not part of uploading textures.

## Alternatives considered

- **Linear workflow now** (`GL_SRGB8_ALPHA8` textures, linear vertex/material colors, `GL_FRAMEBUFFER_SRGB`): physically correct lighting and mip filtering, but the ripple described above. Planned as a later, separate change.
- **sRGB texture formats with a non-sRGB framebuffer:** would mix linear texels with display-space vertex colors; inconsistent.
- **Separate sampler objects from the start:** more objects and API without a current need.

## Consequences

- Lighting in M2.8 is computed on display values: falloff on the dark side is somewhat stronger than in a linear pipeline. For the flat-shaded, color-led style this is acceptable for M2.
- Mipmaps average display values, so downscaled textures get slightly darker than with sRGB-aware filtering.
- Migration path, when lighting quality needs it: request an sRGB-capable framebuffer and enable `GL_FRAMEBUFFER_SRGB`, store color textures as `GL_SRGB8`/`GL_SRGB8_ALPHA8` (a color-space field in `TextureDesc` for data textures), convert vertex and material colors to linear (or author them linear), and re-baseline the GPU tests and screenshots. It needs its own ADR.
- GPU tests inspect textures with OpenGL calls (`tests/rendering/texture_gpu_test.cpp`) until M2.7 can draw them.
