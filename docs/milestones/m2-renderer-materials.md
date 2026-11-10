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
| M2.4 | PNG decoding with stb_image | planned |
| M2.5 | GPU textures behind handles | planned |
| M2.6 | Materials with a base color | planned |
| M2.7 | Texture sampling | planned |
| M2.8 | Directional light and flat shading | planned |
| M2.9 | OBJ subset loader | planned |
| M2.10 | Textured model in island-preview | planned |
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

**Scope:**
- `core::Image` is RGB8 today (frame captures); textures need alpha. Add RGBA8 support; the representation (channel count or a separate type) is decided in this step.
- PNG decoding from memory and from a file with `stb_image` from the `stb` package already in `conanfile.py`, compiled outside the project warning flags like `third_party/stb_image_write/` (ADR 0006), PNG only (`STBI_ONLY_PNG`).
- Imported files are untrusted input: a file size limit, `kMaxImageExtent` checked from the header before decoding, errors that name the file, no crash or unbounded allocation on corrupt data.
- ADR: decoder choice, accepted formats, limits, threat model.

**Tests (headless):** round trip `encodePng` → decode; RGB, RGBA, gray, palette and 16-bit inputs converted to RGBA8; truncated data, wrong signature, zero and oversized dimensions, empty and missing files. Fixtures are generated in the tests where possible.

**Visual check:** none.

### M2.5 — GPU textures behind handles

**Goal:** upload images as textures referenced by handles.

**Scope:**
- `Renderer::createTexture(image, desc) → TextureHandle`, `destroyTexture`, `isValid`; generation-checked like meshes (shared `HandlePool`). Destroying the renderer releases all textures.
- `TextureDesc`: filtering (nearest, linear), wrapping (repeat, clamp to edge), mipmaps on or off.
- `core::Image` rows run top to bottom while UV (0, 0) is bottom-left (M2.1); the upload maps one onto the other.
- Color space (whether textures are stored as sRGB, and whether lighting moves to linear space) is decided here and recorded in an ADR together with the texture API.
- No drawing with textures yet.

**Tests (GPU):** create and destroy, stale and null handles, invalid images rejected with a message, release on renderer destruction.

**Visual check:** none.

### M2.6 — Materials with a base color

**Goal:** draws use a material instead of fixed shader state.

**Scope:**
- `MaterialDesc { baseColor }` → `Renderer::createMaterial` → `MaterialHandle`, `destroyMaterial`; `drawMesh(mesh, material, model)`. Output color = vertex color × base color.
- `drawMesh(mesh, model)` keeps M1 behavior through a built-in white default material, so the overlay and M1 tests are unchanged.
- A stale material handle draws nothing and counts as an error, like a stale mesh handle.
- ADR: what a material holds in M2 and what is deferred to M6.
- Test scene: several objects with different materials.

**Tests (GPU):** base color tints the readback; the default material reproduces M1 output; stale material handles.

**Visual check:** screenshot `docs/screenshots/m2/001-*.png`.

### M2.7 — Texture sampling

**Goal:** a material can have a base-color texture.

**Scope:**
- Optional `MaterialDesc::baseColorTexture`; output color = texture(uv) × vertex color × base color.
- Drawing a textured material with a mesh that has no UVs is an error (counted, logged once), not a silent draw.
- One sampler in the shader; broken-shader fixtures cover it.
- Test scene: one textured object using a small project-made PNG in `assets/textures/` (loaded with M2.4).

**Tests (GPU):** checker texture readback with nearest filtering (exact texels) and linear filtering (tolerance); repeat versus clamp; a material whose texture was destroyed reports the invalid texture.

**Visual check:** screenshot.

### M2.8 — Directional light and flat shading

**Goal:** brightness comes from a light, not from colors baked into the scene.

**Scope:**
- One directional light per frame (direction, color, ambient), set like `ViewDesc`. Lambert diffuse plus ambient from the interpolated normal, which is constant per face for flat-shaded meshes. Normals use the normal matrix, so non-uniform scale stays correct.
- Materials are lit or unlit; the debug overlay stays unlit. A lit material on a mesh without normals is an error, like a texture without UVs.
- The test scene drops its hand-made brightness factors (`boxShades`, `pyramidShades`) and uses the light.

**Tests (GPU):** a face toward the light is brighter than one facing away; ambient is the minimum; unlit materials ignore the light; non-uniform scale.

**Visual check:** screenshot, compared with the M1 look.

### M2.9 — OBJ subset loader

**Goal:** import a simple model made in an external tool.

**Scope:**
- Parse a documented OBJ subset into `MeshData`: `v`, `vt`, `vn` and `f` (triangles and convex polygons, fan-triangulated; `v`, `v/vt`, `v//vn`, `v/vt/vn`; negative indices). Other statements (`o`, `g`, `s`, `usemtl`, `mtllib`, …) are ignored and counted in a warning.
- Every face gets its own vertices (flat shading). Faces without `vn` get a computed flat normal; this step adds the helper that computes it. Vertex colors are white.
- Imported files are untrusted input: limits for file size and element counts, index range checks, non-finite numbers rejected, line numbers in error messages.
- ADR: own parser versus `tinyobjloader` (license, maintenance, Conan availability), the subset and the limits. OBJ is the M2 proof format; glTF 2.0 stays the M6 candidate.

**Tests (headless):** fixtures for every face form, negative indices, quads and polygons, missing normals, malformed lines (with line number), out-of-range indices, oversized counts, NaN/Inf, empty file, CRLF line endings.

**Visual check:** none.

### M2.10 — Textured model in island-preview

**Goal:** prove the import path end to end.

**Scope:**
- A small model and texture made for the project, or with a redistributable license, in `assets/models/` and `assets/textures/`, with their origin documented.
- island-preview loads them at startup and shows them with a lit, textured material next to the test scene. A missing or broken file logs a clear error and the rest of the scene still runs.
- GPU smoke test: island-preview with the model runs without OpenGL errors; a broken-asset fixture produces the expected message.

**Visual check:** screenshot.

### M2.11 — Final verification and pull request

**Goal:** close M2 against its exit criterion. No new features.

**Scope:**
- Full checks: Debug and Release with warnings as errors, headless and GPU tiers in both configurations, format check, CI green on Windows and Linux.
- Every broken-shader fixture still names the shader file or variable.
- `docs/screenshots/m2/README.md` complete; M2 status in `docs/ENGINE_PLAN.md`; `README.md` (current stage, assets); ADR statuses.
- Pull request `milestone/m2-renderer-materials` → `main`, merged only after the user accepts M2.

## Risks and open questions

- Color space of textures and lighting (decided in M2.5).
- The shader interface check must only require inputs and uniforms the shaders use, because the GLSL compiler removes unused ones (M2.3, M2.7).
- `core::Image` has no alpha channel yet (M2.4).
- OBJ files differ between exporters. The subset and limits are tested with fixtures; arbitrary files are not promised to load (M2.9).
- There is no GPU runner in CI: GPU tiers run locally and every sub-milestone report states their result.
