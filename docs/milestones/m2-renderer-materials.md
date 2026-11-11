# M2 — Low-poly renderer and materials

- **Branch:** `milestone/m2-renderer-materials`, created from `main` at `62e9a2a` (M1 merged).
- **Roadmap entry:** `docs/ENGINE_PLAN.md` §10, M2.
- **Workflow:** `docs/DEVELOPMENT_WORKFLOW.md`. One task implements exactly one sub-milestone below, then stops and reports.

## Goal and exit criterion

M1 draws vertex-colored meshes through generation-checked handles; the brightness differences in the test scene are baked into its vertex colors. M2 adds what a lit, partly textured low-poly scene needs: normals and UVs in `MeshData`, textures, a small material model, one directional light with flat shading, and a first model/texture import.

**Exit** (from `docs/ENGINE_PLAN.md`): draw multiple flat-shaded meshes and at least one textured material; shader errors are surfaced clearly.

## Status

| ID | Title | Status |
|---|---|---|
| M2.1 | Optional normals and UVs in `MeshData` | done |
| M2.2 | Flat normals and UVs for the primitives | done |
| M2.3 | Upload normals and UVs to the GPU | done |
| M2.4 | PNG decoding with stb_image | done |
| M2.5 | GPU textures behind handles | done |
| M2.6 | Materials with a base color | done |
| M2.7 | Texture sampling | done |
| M2.8 | Directional light and flat shading | done |
| M2.9 | OBJ subset loader | done |
| M2.10 | Textured model in island-preview | in progress |
| M2.11 | Final verification and pull request | planned |

Status values: `planned`; `in progress` (set when work starts and committed with the change); `done` (set by a separate `docs(milestones): mark M2.x done (M2.x)` commit after the change is pushed and its CI is green, `docs/DEVELOPMENT_WORKFLOW.md` §4). `git log --grep "(M2.x)"` finds the commits.

## Rules for all of M2

- No OpenGL type or header leaves `engine/src/render/opengl/` (AGENTS.md §3). Textures and materials are referenced by generation-checked handles, like meshes (ADR 0007). Handles are never serialized.
- Vertex colors stay first-class. Every material multiplies with them, and a mesh without a texture is a normal case, not a fallback (AGENTS.md §6).
- An optional `MeshData` attribute is either absent (empty array) or has one element per position (M2.1).
- Flat shading comes from per-face normals on vertices that faces do not share (ENGINE_PLAN §5). Nothing is inferred from the triangulation.
- Materials are created in code. Material files and authoring belong to M6.
- Every change to the shader interface updates the broken-shader fixtures in `tests/rendering/fixtures/`, so errors keep naming the shader file or variable (part of the exit criterion).
- Assets committed to the repository (test textures, the M2.10 model) are made for this project or carry a license that allows redistribution; their origin is recorded in `docs/references/THIRD_PARTY_NOTICES.md`.
- Decisions about dependencies, public APIs and file formats get an ADR in the sub-milestone that makes them (AGENTS.md §10), numbered when written.

## Sub-milestones

### M2.1 — Optional normals and UVs in `MeshData`

**Goal:** `MeshData` can carry per-vertex normals and texture coordinates, with clear validity rules, before anything produces or uploads them.

**Scope** (final specification, approved 2026-11-10):
- New arrays `normals` (`Vec3`) and `uvs` (`Vec2`). Each is empty (absent) or holds exactly one element per position; a mesh without positions has neither. Queries `hasNormals()` and `hasUvs()`. Missing attributes are never synthesized.
- `validate()` reports the first failure in a fixed order: vertex count, color count, normal count, UV count, whole triangles, index range, then vertex by vertex position, color, normal, UV. Normals are finite with length 1 ± `kNormalLengthTolerance` (1e-3); validation never normalizes them. UVs are finite, any range (tiling). Messages name the attribute and the vertex, e.g. `mesh has 3 positions but 2 normals (expected 0 or 3)`, `mesh normal 1 has length 0.5, expected 1 +/- 0.001`, `mesh uv 2 is not finite`.
- `append()` returns `std::optional<core::Error>` instead of `void`. It concatenates normals and UVs; a mesh without vertices matches any layout (an empty target takes the source's layout, an empty source changes nothing). It fails, leaving the target unchanged, if both meshes have vertices but only one has normals or UVs, if the result would exceed 4294967295 vertices, or if a shifted index would wrap around 32 bits. All arrays are reserved before anything is inserted. Appending a mesh to itself works.
- The size arithmetic sits in the internal header `engine/src/geometry/MeshLimits.hpp` and is unit-tested directly.
- UV convention documented in the header: (0, 0) is the bottom-left corner of the image, U points right, V points up (OBJ/OpenGL convention).
- Amendment 1 to ADR 0007.

**Out of scope:** GPU upload and shader inputs (M2.3), primitives that produce normals/UVs (M2.2), computing normals, tangents, more than one UV set.

**Acceptance:** existing producers (primitives, debug text, test scene) and the renderer work unchanged, and all M1 tests pass. New unit tests cover valid and invalid counts, non-finite and non-unit normals, both sides of the tolerance, UVs outside [0, 1], `append()` for every attribute combination, empty meshes and self-append.

**Visual check:** none, no visible change. The GPU tier runs as a regression check, because `Renderer::createMesh` and `updateMesh` call `validate()`.

### M2.2 — Flat normals and UVs for the primitives

**Goal:** the test-scene primitives carry what lighting and texturing need.

**Scope** (decisions approved 2026-11-10):
- `makeBox`, `makePyramid` and `makeCheckerPlane` always emit normals and UVs (no option to leave them out). Each face has one outward unit normal, copied to the face's own vertices.
- Normals: the face axes for the box and the plane; for the pyramid sides the normalized cross product of the face edges, in a local function in `Primitives.cpp`. It computes in double, so very small or very large but finite sizes (1e-30, 1e30) still give unit normals instead of NaN.
- UV orientation: seen from outside, U points right and V up on every face, so textures are never mirrored; on vertical faces (box sides, pyramid sides) V is world +Y. Box top: U = +X, V = -Z; box bottom and pyramid base (seen from below): U = +X, V = +Z.
- UV extent: every box face maps the whole unit square, whatever its size (a texture stretches on long faces); pyramid sides map (0, 0), (1, 0) along the base edge and (0.5, 1) at the apex; the pyramid base maps the unit square; the checker plane uses one UV unit per cell (up to 1024), from (0, 0) at the (-X, +Z) corner, with colorA still at the (-X, -Z) corner.
- The debug text mesh and the test scene stay unchanged (the scene's brightness factors remain until M2.8); the renderer still ignores normals and UVs until M2.3.
- Public API unchanged; `Primitives.hpp` documents the normals and UVs. No ADR.

**Out of scope:** GPU upload, a general public normal-computation helper (needed first by M2.9).

**Acceptance:** every normal has unit length, is perpendicular to its face and points to the side from which the face winds counter-clockwise; UVs are finite and in the documented ranges; vertex and index counts are unchanged; `validate()` passes.

**Visual check:** none, the renderer ignores the new attributes.

### M2.3 — Upload normals and UVs to the GPU

**Goal:** the OpenGL backend uploads the optional attributes and the mesh shaders receive them.

**Scope** (decisions taken while implementing, ADR 0007 Amendment 2):
- `GlMesh` stores normals and UVs as further ranges of its vertex buffer (same structure-of-arrays layout as today) at attribute locations 2 (`aNormal`) and 3 (`aUv`), one buffer binding per attribute. The buffer always reserves space for all four ranges (about 48 instead of 28 bytes per vertex), so adding or removing an optional attribute in an update reuses the buffers.
- Absent attributes are disabled per mesh, and the shader reads fixed defaults set once at renderer startup: normal (0, 0, 0), meaning "no normal", and UV (0, 0).
- `mesh.vert` declares the inputs with explicit locations but does not use them yet; the frame looks the same as before.
- Startup interface check: `aPosition`, `aColor` and `uModelViewProjection` stay required. `aNormal` and `aUv` may be inactive (the compiler drops unused inputs), but an active one must be at its location, else startup fails naming the input and both locations. New broken-shader fixture `wrong-attribute-location`.
- GPU tests: meshes with and without normals/UVs draw the same with the default shaders; test-only shaders (`normal-as-color`, `uv-as-color` fixtures) prove the data arrives and that meshes without the attribute read the defaults, also side by side in one frame; updates add and remove normals and UVs.

**Visual check:** island-preview looks as in `docs/screenshots/m1/004-debug-overlay.png`, compared by eye; no new screenshot.

### M2.4 — PNG decoding with stb_image

**Goal:** load PNG files into `core::Image` safely.

**Scope** (decisions taken while implementing, ADR 0008):
- `core::Image` gains `PixelFormat format` (`Rgb8`, the default, or `Rgba8`), the last member, so `Image{width, height, pixels}` stays the RGB frame capture; `validate()` and `encodePng()` handle both formats.
- `decodePng(std::span<const std::uint8_t>)` and `readPng(path)` return RGBA8, top row first, for every PNG color type and bit depth, using `stb_image` (v2.30) from the `stb` package already in `conanfile.py`. Compiled once in `third_party/stb_image/` without project warning flags, with `STBI_ONLY_PNG`, `STBI_NO_STDIO`, `STBI_NO_LINEAR`; the project never changes stb's global settings.
- Untrusted input: data/files up to `kMaxPngFileBytes` (256 MiB, file size checked before reading); own signature check; header read with `stbi_info_from_memory` before decoding, each side in 1..`kMaxImageExtent` (16384) and at most `kMaxPngPixels` (64 Mi pixels, 256 MiB as RGBA) in total; errors with stb's short reason, `readPng` errors name the file; allocation failures become errors.
- ADR 0008: decoder choice, PNG only, limits, threat model and residual risks.

**Tests (headless, `tests/unit/core/png_decode_test.cpp` and `image_test.cpp`):** round trips `encodePng` → decode for RGB and RGBA; row order; gray (8 and 1 bit), gray + alpha, palette with and without tRNS, 16-bit RGB and RGBA converted to RGBA8; not-a-PNG inputs and empty data; truncated files; zero, oversized and too-many-pixels headers; the largest width decodes; every byte of two valid files flipped and every truncation length tried (error or valid image, never a crash); `readPng` with a non-ASCII path, missing file, folder, non-PNG file and a file above the size limit; RGBA validation and the color type `encodePng` writes. Fixtures are built by the tests (a small PNG writer with stored deflate blocks).

**Visual check:** none.

### M2.5 — GPU textures behind handles

**Goal:** upload images as textures referenced by handles.

**Scope** (decisions taken while implementing, ADR 0009):
- `Renderer::createTexture(image, desc = {}) → TextureHandle`, `destroyTexture`, `isValid(TextureHandle)`; generation-checked like meshes (shared `HandlePool`). Destroying the renderer releases all textures. Invalid images are rejected with the `core::validate()` reason.
- `TextureDesc`: `filter` (`Nearest`, `Linear`), `wrap` (`Repeat`, `ClampToEdge`), `mipmaps`; defaults `Linear`, `Repeat`, mipmaps on.
- `GlTexture`: immutable `GL_RGB8`/`GL_RGBA8` storage; rows uploaded bottom up (one call per row, no flipped copy), so V = 0 is the image's bottom row; full mip chain generated when enabled; sampling state on the texture object.
- Color space: M2 stays in display space, as M1 — textures without sRGB decoding, no sRGB framebuffer, lighting (M2.8) on the same values. The migration path to a linear pipeline is recorded in ADR 0009.
- No drawing with textures yet.

**Tests (GPU, `tests/rendering/texture_gpu_test.cpp`, OpenGL introspection):** create and destroy, null and stale handles with slot reuse, invalid images rejected with a message and not uploaded, release on renderer destruction, bottom row at V = 0 (RGB with 3-byte rows and RGBA), filter/wrap/mip levels/internal format for every option, largest size 16384 with 15 mip levels.

**Visual check:** none.

### M2.6 — Materials with a base color

**Goal:** draws use a material instead of fixed shader state.

**Scope** (decisions taken while implementing, ADR 0010):
- `MaterialDesc { baseColor }` (RGBA, default white, each component finite and in [0, 1], checked by the GPU-free `validate(MaterialDesc)`) → `Renderer::createMaterial` → `MaterialHandle`, `destroyMaterial`, `isValid`; `drawMesh(mesh, material, model)`. Output color = vertex color × base color. Materials are plain data in the renderer (no GPU object); the base color is the uniform `uBaseColor` of `mesh.frag`, set per draw and required by the startup interface check (new fixture `missing-base-color`; the other test shaders declare it too).
- `drawMesh(mesh, model)` keeps M1 behavior through a built-in white default material, so the overlay and M1 tests are unchanged.
- An invalid mesh or material handle draws nothing and counts as one error, logged once per kind.
- ADR 0010: what a material holds in M2 and what is deferred to M6.
- Test scene: shapes keep brightness factors as gray vertex colors and take their hue from materials (same look as M1); the ground keeps its vertex colors with a white material; meshes are shared (cubes, pillars) and five swatch cubes share one mesh with five materials. The overlay subtitle says M2.

**Tests:** unit (`tests/unit/render/material_test.cpp`): default material, valid and invalid components with messages. GPU: base color tints the readback, one mesh with two materials in one frame, the default and a white material reproduce the vertex colors and a tint does not leak into later default draws, material alpha blends, destroyed/null/reused material handles and invalid meshes count one error each, invalid colors rejected with a reason; smoke test for the missing `uBaseColor`.

**Visual check:** screenshot `docs/screenshots/m2/001-materials.png`.

### M2.7 — Texture sampling

**Goal:** a material can have a base-color texture.

**Scope** (decisions taken while implementing, ADR 0010 Amendment 1):
- Optional `MaterialDesc::baseColorTexture` (a `TextureHandle`, null by default; the material refers to the texture, it does not own it); output color = texture(uv) × vertex color × base color, alpha included. `createMaterial` rejects a texture handle that is neither null nor valid.
- Drawing a textured material with a mesh that has no UVs is an error (counted, logged once), not a silent draw. So is drawing a material whose texture was destroyed; both draw nothing, like invalid mesh and material handles.
- One sampler in the shader: `uniform sampler2D uBaseColorTexture` on texture unit 0, required by the startup interface check (new fixture `missing-base-color-texture`; the other test shaders declare and sample it). Untextured materials and the default material bind a built-in 1×1 white texture, so the shader has no branch and their output is unchanged. `mesh.vert` now passes the UV on.
- Test scene: a crate (the shared cube mesh, white base color) with the 16×16 pixel-art `assets/textures/crate.png` made for the project, nearest filtering, loaded with M2.4 at startup; a missing or broken file stops island-preview with the reason, like a missing shader.

**Tests:** unit: the default material is untextured; `validate` ignores the texture handle but still checks the color. GPU: checker texture readback with nearest filtering (exact texels, orientation) and linear filtering (every column of a black-to-white ramp within tolerance); repeat versus clamp with UVs up to 2; texture × vertex color × base color per channel, and no texture leaking into later untextured or default draws; texture alpha blends; a textured material on a mesh without UVs counts an error until an update adds UVs; a material whose texture was destroyed counts an error, also after the slot is reused, and a stale texture handle is rejected by `createMaterial`. The texture tests now set aside the renderer's own white texture and check that destroying the renderer releases it. Smoke test for the missing `uBaseColorTexture`.

**Visual check:** screenshot `docs/screenshots/m2/002-textured-crate.png`.

### M2.8 — Directional light and flat shading

**Goal:** brightness comes from a light, not from colors baked into the scene.

**Scope** (decisions taken while implementing, ADR 0011):
- One directional light per frame: `LightDesc { direction, color, ambient }` set with `Renderer::setLight`, like `ViewDesc`. `beginFrame` resets it to `LightDesc{}`, which is neutral (white ambient, no direct light). Direction any non-zero length, colors finite and `>= 0`, checked by the GPU-free `validate(LightDesc)`; an invalid light is ignored and counts one error, the previous light stays. Lambert diffuse plus ambient from the interpolated normal, which is constant per face for flat-shaded meshes; alpha is not lit. Normals use the cofactor matrix of the model matrix (`uNormalMatrix`), so non-uniform scale and mirroring stay correct and a flattened model stays defined.
- Materials are lit or unlit: `MaterialDesc::shading`, `Shading::Unlit` by default, so the debug overlay, the default material and earlier materials are unchanged. Unlit draws use the same shader with white ambient and no light. A lit material on a mesh without normals is an error, like a texture without UVs.
- Shader interface: `uNormalMatrix`, `uDirectionToLight`, `uLightColor`, `uAmbientColor` are required at startup (new fixture `missing-lighting`, the M2.7 shaders; the other fixtures declare them).
- The test scene drops its hand-made brightness factors (`boxShades`, `pyramidShades`): white vertex colors, lit materials and a sunlight chosen by eye to stay close to the earlier look.

**Tests:** unit (`tests/unit/render/light_test.cpp`): the default light, valid and invalid lights with messages; the normal matrix under translation, rotation, uniform and non-uniform scale, shear, mirroring and a flattened model. GPU: ambient plus Lambert diffuse for a face toward the light, one at 60 degrees and one facing away (ambient is the minimum); light color and ambient per channel, alpha not lit; unlit materials and the default material ignore the light, the default light leaves lit materials unchanged and `beginFrame` resets it; normals follow rotation and non-uniform scale; a lit material on a mesh without normals counts an error until an update adds normals; an invalid light is ignored and counted. Smoke test for the missing lighting uniforms.

**Visual check:** screenshot `docs/screenshots/m2/003-directional-light.png`, compared with the M1 look.

### M2.9 — OBJ subset loader

**Goal:** import a simple model made in an external tool.

**Scope** (decisions taken while implementing, ADR 0012):
- Own parser, no new dependency (`tinyobjloader` would still need the conversion, limits and error handling around it): `geometry::parseObj(text, limits)` and `readObj(path, limits)` → `ObjMesh { mesh, ignoredStatements, ignoredKeywords }`, in the geometry module since there is no asset registry yet.
- Parse a documented OBJ subset into `MeshData`: `v x y z [w]`, `vt u [v [w]]`, `vn` (normalized on import) and `f` (triangles and convex polygons, fan-triangulated; `v`, `v/vt`, `v//vn`, `v/vt/vn`, one form per face; negative indices; indices refer to earlier elements). Either all faces have UVs or none. Other statements (`o`, `g`, `s`, `usemtl`, `mtllib`, …) are ignored, counted and listed; `readObj` logs them in one warning.
- Every face gets its own vertices (flat shading). Faces without `vn` get a computed flat normal from the new public helper `geometry::faceNormal` (area vector in double, relative degeneracy threshold `kDegenerateFaceTolerance` = 1e-10), which the primitive builders now use too; a degenerate face without normals is an error. Vertex colors are white.
- Imported files are untrusted input: `ObjLimits` (64 MiB of text, checked on the file size before reading; 1,000,000 elements of each kind; 1,000,000 face corners), index range checks, non-finite and out-of-range numbers rejected, line numbers in error messages, quoted tokens shortened and sanitized, out of memory reported as an error. Input without faces is an error.
- ADR 0012: the parser choice, the subset and the limits. OBJ is the M2 proof format; glTF 2.0 stays the M6 candidate.

**Tests (headless):** `tests/unit/geometry/obj_import_test.cpp` with inline fixtures for every face form, negative indices, quads and polygons, own vertices per face, missing normals, comments/tabs/CRLF/optional components, skipped statements and the keyword cap, malformed lines (with line number), out-of-range and forward indices, NaN/Inf and out-of-range numbers, every limit (and exactly at it), empty input and input without faces, and `readObj` with a temp file (errors name the file, size checked first). `tests/unit/geometry/face_normal_test.cpp`: triangles, polygons with a collinear start, bent quads, degenerate and non-finite input, tiny, huge and thin polygons.

**Visual check:** none.

### M2.10 — Textured model in island-preview

**Goal:** prove the import path end to end.

**Scope** (decisions taken while implementing):
- A small model and texture made for the project, or with a redistributable license, in `assets/models/` and `assets/textures/`, with their origin documented: an octagonal lighthouse, `assets/models/lighthouse.obj` (7.8 m tall, 45 faces as quads, triangles and octagons with `v/vt/vn`, 102 triangles, only statements of the subset), and a 32×32 texture atlas `assets/textures/lighthouse.png` (brick stripes with a window, lantern glass, roof, stone), both written by a script during M2.10 (`docs/references/THIRD_PARTY_NOTICES.md`).
- `.gitignore` ignored `*.obj` for MSVC object files; OBJ models under `assets/` and in test fixtures are now excepted.
- island-preview loads them at startup and shows them with a lit, textured material (white base color, nearest filtering) next to the test scene, at (13, 0, -10) right of the red cube. A missing or broken file logs a clear error and the rest of the scene still runs: a model that cannot be loaded is left out, a texture that cannot be loaded leaves its materials untextured. This also replaces M2.7's rule that a missing `crate.png` stops the start. `--model FILE.obj` loads another OBJ file instead of the scene's model, so tests need no copy of the shaders.
- GPU smoke test: island-preview with the model runs without OpenGL errors and now fails on any logged error (`FAIL_REGULAR_EXPRESSION "\[error\]"`), so a broken committed asset is caught; a broken-asset fixture (`tests/rendering/fixtures/broken-model/broken.obj`) produces the expected message with the line and still exits 0; a missing model file is named.
- Headless (runs in CI, unlike the GPU tier): `tests/unit/sandbox/island_preview_assets_test.cpp` checks that the committed textures decode and that the lighthouse loads without skipped statements, with UVs in [0, 1], within its documented bounds and with normals that agree with the counter-clockwise winding.

**Visual check:** screenshot `docs/screenshots/m2/004-imported-model.png`.

### M2.11 — Final verification and pull request

**Goal:** close M2 against its exit criterion. No new features.

**Scope:**
- Full checks: Debug and Release with warnings as errors, headless and GPU tiers in both configurations, format check, CI green on Windows and Linux.
- Every broken-shader fixture still names the shader file or variable.
- `docs/screenshots/m2/README.md` complete; M2 status in `docs/ENGINE_PLAN.md`; `README.md` (current stage, assets); ADR statuses.
- Pull request `milestone/m2-renderer-materials` → `main`, merged only after the user accepts M2.

## Risks and open questions

- Color space of textures and lighting: decided in M2.5 — display space for all of M2, linear pipeline later as its own change (ADR 0009).
- The shader interface check must only require inputs and uniforms the shaders use, because the GLSL compiler removes unused ones (M2.3, M2.7).
- Alpha in images: resolved in M2.4 (`PixelFormat::Rgba8`); RGBA textures blend with their alpha since M2.7.
- OBJ files differ between exporters. The subset and limits are tested with fixtures; arbitrary files are not promised to load (M2.9).
- There is no GPU runner in CI: GPU tiers run locally and every sub-milestone report states their result.
