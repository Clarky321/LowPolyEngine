# ADR 0010: Material model in M2

- **Status:** Accepted; amended for M2.7 (Amendment 1)
- **Date:** 2026-11-11
- **Related:** ADR 0007 (handles), ADR 0009 (textures, display-space colors), `docs/milestones/m2-renderer-materials.md` M2.6–M2.8, `docs/ENGINE_PLAN.md` §5 and M6, AGENTS.md §6

## Context

Until M2.6 every draw used fixed shader state: the output was the vertex color. M2 needs textured (M2.7) and lit (M2.8) surfaces, and AGENTS.md §6 asks for vertex colors and textured materials side by side, with data-driven palettes rather than colors in shaders. The first step is a material that a draw refers to.

## Decision

- **A material is renderer-owned data behind a handle.** `Renderer::createMaterial(MaterialDesc) → MaterialHandle`, `destroyMaterial`, `isValid(MaterialHandle)`. Handles are generation-checked like meshes and textures (same `HandlePool`), never serialized. A material owns no GPU object: its values are set as shader uniforms on every draw.
- **What a material holds in M2:** `baseColor` (RGBA, each component finite and in [0, 1], checked by the GPU-free `validate(MaterialDesc)`). M2.7 adds an optional base-color texture, M2.8 a lit/unlit choice. Nothing else is planned for M2.
- **Color rule:** output color = vertex color × base color (× texture color from M2.7), alpha included, in display space (ADR 0009). Vertex colors stay first-class: a white material shows them unchanged, a gray vertex shade times a colored material gives tinted shading, and one mesh can be drawn with many materials.
- **Drawing:** `drawMesh(mesh, material, model)`. `drawMesh(mesh, model)` keeps M1 behavior through a built-in white default material, so overlays and existing callers need no material. An invalid mesh or material handle draws nothing and counts as one error, logged once per kind.
- **Shader interface:** the fragment shader declares `uniform vec4 uBaseColor`; the renderer requires it at startup like `uModelViewProjection` and fails with the uniform's name otherwise (fixture `missing-base-color`).
- **Materials are created in code** in M2. One shader program serves all materials.

## Deferred to M6 (content workflow) or later

- Material files and assets with stable project IDs, loading and saving, an editor for them.
- Further parameters (emissive, roughness, …), per-material shaders or a material graph (the graph is excluded by AGENTS.md §11 until the island milestone is accepted).
- Sorting or batching draws by material, uniform buffers; added when profiling asks for them.
- Updating a material in place; materials are recreated for now.

## Consequences

- The test scene moves hues from vertex colors into materials and shares meshes between objects (`sandbox/island-preview/TestScene.cpp`).
- Every shader the renderer loads, test fixtures included, must provide `uBaseColor`.
- Per draw: one more uniform upload. Irrelevant at the current object counts.

## Amendment 1: base color texture (M2.7, 2026-11-11)

- **`MaterialDesc::baseColorTexture`** is a `TextureHandle`; null (the default) means no texture. The material refers to the texture and does not own it, like an object refers to a shared mesh. `createMaterial` rejects a handle that is neither null nor valid, naming its index and generation; `validate(MaterialDesc)` stays GPU-free and does not judge handles.
- **Color rule:** output color = texture(UV) × vertex color × base color, alpha included (RGB textures have alpha 1). Texels are used as stored, in display space (ADR 0009).
- **One sampler, no branch.** The fragment shader declares `uniform sampler2D uBaseColorTexture`, read from texture unit 0; the renderer requires it at startup like the other uniforms (fixture `missing-base-color-texture`) and sets its unit once. Untextured materials, and `drawMesh(mesh, model)`, bind a built-in 1×1 white texture, so every draw samples a texture and the result for them is unchanged. The white texture is created with the renderer, is not in the handle pool and is released with it.
- **Draw errors.** A textured material whose texture was destroyed, or a textured material on a mesh without UVs, draws nothing and counts one error, logged once per kind with the handles involved. A texture created later in the same slot does not revive the material (generation check). Untextured materials need no UVs.
- **Sampling state** (filter, wrap, mipmaps) stays on the texture (`TextureDesc`, ADR 0009); a material has no sampler settings of its own.
- **Test scene:** a crate drawn with the project-made 16×16 `assets/textures/crate.png` (nearest filtering), a white base color and the cube's gray vertex shades. island-preview loads its textures at startup from `<assets>/textures/`; a missing or broken file stops the start with the reason, like a missing shader. (Changed in M2.10: the error is logged and the materials are drawn without the texture; see the M2.10 section of `docs/milestones/m2-renderer-materials.md`.)
