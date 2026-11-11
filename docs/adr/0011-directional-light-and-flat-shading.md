# ADR 0011: Directional light and flat shading in M2

- **Status:** Accepted
- **Date:** 2026-11-11
- **Related:** ADR 0007 (normals in `MeshData`), ADR 0009 (display-space colors), ADR 0010 (materials), `docs/milestones/m2-renderer-materials.md` M2.8, `docs/ENGINE_PLAN.md` §5, AGENTS.md §6

## Context

Until M2.8 the brightness of each face was baked into the test scene as gray vertex colors (`boxShades`, `pyramidShades`), so a rotating object kept its shading and every new shape needed hand-made factors. AGENTS.md §6 asks for a small forward renderer with one directional light and flat shading; ENGINE_PLAN §5 defines flat shading as per-face normals on vertices that faces do not share, which the primitives have had since M2.2.

## Decision

- **One directional light per frame.** `LightDesc { direction, color, ambient }` is set with `Renderer::setLight`, like `ViewDesc` with `setView`. `direction` is the world-space direction the light travels in (any non-zero length, normalized by the renderer); `color` and `ambient` are RGB, each component finite and `>= 0`, with no upper bound (results above 1 saturate in the 8-bit framebuffer). The GPU-free `validate(LightDesc)` checks this and names the first offending value.
- **Defaults are neutral.** `LightDesc{}` is white ambient and no direct light, so lit materials look unlit until a light is set. `beginFrame` resets the light to it, so no light leaks from one frame into the next.
- **An invalid light** passed to `setLight` is ignored: the previous light stays, the call counts one error in `errorCount()` and the first one is logged. A per-frame call does not return an error the caller would have to check every frame; draws with invalid handles behave the same way.
- **Lighting model:** for lit materials, output RGB = material color × (ambient + light color × max(0, n · l)), where n is the normalized interpolated normal and l the unit vector towards the light; alpha is not lit. On a flat-shaded mesh n is constant per face, so this is flat shading without any flat-shading switch; smooth normals (an imported model, M2.9) would shade smoothly with the same code. Values are in display space (ADR 0009).
- **Normals in world space** use the cofactor matrix of the model matrix's upper 3×3 part, negated when its determinant is negative. It equals the inverse transpose times the determinant, so after normalization it gives the same, correct directions under non-uniform scale and mirroring, and unlike the inverse it stays defined when the model flattens an object (determinant 0). The renderer computes it per draw (`engine/src/render/Lighting.cpp`, unit-tested) and uploads it as `uniform mat3 uNormalMatrix`; the fragment shader normalizes after interpolation.
- **Lit or unlit is a material property:** `MaterialDesc::shading` is `Shading::Unlit` (default) or `Shading::Lit`. Unlit is the default so the built-in material of `drawMesh(mesh, model)`, the debug overlay and every material made before M2.8 keep their meaning (ADR 0010: a white material shows the vertex colors).
- **Unlit draws use the same shader** with white ambient and no light color, so the light factor is 1; there is no branch and no extra uniform. A zero normal (a mesh without normals) gets no diffuse term instead of NaN.
- **A lit material on a mesh without normals** draws nothing and counts one error, logged once, like a textured material on a mesh without UVs (ADR 0010 Amendment 1). Unlit materials need no normals.
- **Shader interface:** `mesh.vert` declares `uniform mat3 uNormalMatrix`, `mesh.frag` `uniform vec3 uDirectionToLight`, `uLightColor` and `uAmbientColor`. The renderer requires all four at startup, after the material uniforms; the fixture `missing-lighting` (the M2.7 shaders) is reported by the first one, `uNormalMatrix`. `uDirectionToLight` is uploaded when the light changes, the other three per draw.
- **Test scene:** the hand-made brightness factors are gone; shapes have white vertex colors and lit materials. Its sunlight was chosen by eye to stay close to the earlier factors (`sandbox/island-preview/TestScene.cpp`). The old factors were not consistent with any single light, so the look is similar to M1, not identical.

## Deferred

- Several lights, point and spot lights, shadows, specular highlights, fog: added one at a time when a milestone needs them (AGENTS.md §6).
- Linear-space lighting with an sRGB framebuffer (ADR 0009 describes the migration).
- Per-object or per-material light settings, light data in uniform buffers.

## Consequences

- Every shader the renderer loads, test fixtures included, provides the four lighting uniforms.
- Per draw: a 3×3 cofactor matrix on the CPU and three more uniform uploads. Irrelevant at the current object counts.
- Brightness now depends on orientation: the spinning cube in the test scene changes its shading as it turns.
