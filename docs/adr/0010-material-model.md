# ADR 0010: Material model in M2

- **Status:** Accepted
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
