# ADR 0007: GLM math types, CPU-side MeshData and renderer mesh handles

- **Status:** Accepted; amended for M2.1 (Amendment 1)
- **Date:** 2026-11-10
- **Related:** ADR 0001 (GLM candidate), ADR 0003 (WorldGen is pure CPU), `docs/ENGINE_PLAN.md` §5/§6, `docs/PROJECT_STRUCTURE.md`

## Context

M1 replaces the hard-coded test triangle with real mesh rendering and adds a camera. This needs vector/matrix math, a CPU geometry format, and a way for engine code to refer to GPU meshes. Later, world generation (M3) produces terrain, water and cloud geometry and must not depend on OpenGL, SDL or GPU handles (ADR 0003): `WorldGen → MeshData → Renderer → GPU`.

## Decisions

### 1. GLM for math, behind `engine::math` aliases

`glm/1.0.3` from Conan Center (MIT or Happy Bunny license, header-only, all platforms). The standard library has no vector/matrix types; a project-owned math library would be several hundred lines of well-known code to write and test. GLM was the planned candidate (ADR 0001, `docs/TECH_STACK.md` §4).

- `engine/core/Math.hpp` defines `math::Vec2/Vec3/Vec4/Mat4`; engine headers use these names. GLM functions (`lookAt`, `perspective`, …) are used in `.cpp` files and tests.
- Conventions: right-handed, +Y up, radians, column vectors, OpenGL clip depth [-1, 1]. No GLM configuration macros are set, so every target sees the same types.
- GLM is a PUBLIC dependency of `engine`, because public headers name its types. It contains no GPU or platform code.
- Generation code that must be bit-reproducible (ADR 0003) still follows that ADR's floating-point rules; GLM does not change them.

### 2. `geometry::MeshData` is the CPU ↔ renderer hand-over format

`engine/geometry/MeshData.hpp`, a new `geometry` module that depends only on `core` (math, results):

- Structure of arrays: `positions` (Vec3), `colors` (RGBA Vec4), `indices` (uint32 triangle list, counter-clockwise front faces). New attributes (normals and UVs for M2) are added as further arrays.
- `validate()` checks the invariants every consumer relies on (one color per position, whole triangles, indices in range, finite values) and names the first offending element. The renderer validates every upload.
- Flat shading is explicit: faces that should look flat get their own vertices. Nothing is inferred from triangulation (`docs/ENGINE_PLAN.md` §5).
- It lives outside `render/` so that WorldGen can produce it without including any renderer header.

### 3. Renderer meshes are referenced by generation-checked handles

`Renderer::createMesh(MeshData) → MeshHandle`, `updateMesh`, `destroyMesh`, `drawMesh(handle, model)`.

- `MeshHandle` is `{index, generation}`. Destroying a mesh bumps its slot's generation, so a stale handle is detected (drawing it is counted as an error and logged once) instead of silently drawing whatever reuses the slot.
- Handles are runtime identities only. They are never serialized; persisted assets get stable project IDs (AGENTS.md §3).
- No OpenGL type appears in `Renderer.hpp`. The OpenGL backend (`GlMesh`) keeps positions and colors in one buffer as two ranges (the same layout as `MeshData`, no interleaving copy), indices in a second buffer, both with spare capacity so per-frame updates (debug text) reuse them.
- Pipeline state per draw batch is set with `setView(ViewDesc)`: view/projection matrices, depth mode, blend mode. No material system yet; that is M2.

## Alternatives considered

- **Own math library:** full control, but no current need justifies the code and tests. Revisit only if GLM blocks a requirement.
- **Interleaved `Vertex` struct in MeshData:** simplest to upload, but every new attribute changes all producers. Interleaving can still happen in the backend if profiling shows a benefit.
- **RAII `Mesh` objects owning GPU buffers:** ties object lifetime to the GL context and makes engine/editor data structures carry GPU objects; handles keep GPU ownership inside the renderer.

## Consequences

- One new header-only dependency, recorded in `docs/references/THIRD_PARTY_NOTICES.md`.
- `docs/PROJECT_STRUCTURE.md` gains `engine/geometry/`; WorldGen will depend on `core` + `geometry` only.
- The shader interface (`aPosition` at 0, `aColor` at 1, `uModelViewProjection`) is checked when the renderer starts; mismatches fail with a named variable instead of an empty frame.
- Revisit when M2 adds normals/UVs and materials (likely more `MeshData` arrays and a material handle next to the mesh handle).

## Amendment 1: optional normals and UVs (M2.1, 2026-11-10)

M2 needs per-vertex normals (lighting, M2.8) and texture coordinates (texturing, M2.7). They are added as further `MeshData` arrays, as decision 2 foresaw.

- **Optional, empty or one per vertex.** `normals` (`Vec3`) and `uvs` (`Vec2`) are optional; positions and colors stay required. An empty array means the attribute is absent; otherwise it holds exactly one entry per position. `hasNormals()` and `hasUvs()` report presence. Nothing fills in a missing attribute: producers that have normals or UVs provide them, and the others (debug text, and the primitives until M2.2) stay unchanged.
- **Normals have unit length within `kNormalLengthTolerance` (1e-3).** Producers normalize; `validate()` only checks and never changes data. The tolerance accepts float rounding (about 1e-7) and normals read from text formats (up to about 1e-4); a length error of 1e-3 changes diffuse lighting by at most 0.1 %, less than one 8-bit color step. Zero and unnormalized normals fail with the vertex index and the measured length. A normal does not have to match its face (smooth normals of imported models are allowed).
- **UV convention.** (0, 0) is the bottom-left corner of the image, U points right, V points up (OBJ/OpenGL). Values outside [0, 1] are valid and repeat the texture. `core::Image` rows run top to bottom; mapping one onto the other is the job of the texture upload (M2.5).
- **Fixed validation order**, documented in the header, so the reported error is predictable: vertex count, color count, normal count, UV count, whole triangles, index range, then vertex by vertex position, color, normal, UV.
- **`append()` returns `std::optional<core::Error>`**, like `validate()`. `core::Result` has no `void` form, and one function does not justify adding it. Two meshes with vertices must agree on which optional attributes they have; a mesh without vertices matches any layout. `append()` also fails if the result would exceed 4294967295 vertices or a shifted index would wrap around 32 bits. On failure the target is unchanged: every check runs before the first change, and all arrays are reserved before anything is inserted. Appending a mesh to itself works (the source is copied first).
- The size arithmetic lives in the internal header `engine/src/geometry/MeshLimits.hpp`, so the 32-bit limits are unit-tested with plain numbers instead of meshes with billions of vertices.

The renderer still uploads only positions and colors; uploading normals and UVs is M2.3. Every mesh it receives is validated with the rules above.
