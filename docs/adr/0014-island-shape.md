# ADR 0014: Island shape — outline, top surface and underside

- **Status:** Accepted; amended for M3.6 (Amendment 1) and M3.8 (Amendment 2)
- **Date:** 2026-11-13
- **Related:** ADR 0003 (determinism, noise in Amendment 2), ADR 0013 (settings, island anchor and volume, placement in Amendment 1), `docs/milestones/m3-floating-archipelago.md` M3.5 and M3.6, `docs/ENGINE_PLAN.md` §6

## Context

M3.4 places islands as bounding cylinders. M3.5 gives each island its shape as height data: an irregular outline, a top surface with hills, mountains, cliffs and plateaus, and a voluminous rocky underside that is never a flat disc or a plain inverted cone. The mesh of M3.6 samples these values; the shape must therefore be well defined at every point, continuous, inside the island's volume, and the same on every platform.

## Decision

`engine/worldgen/IslandShape.hpp`: `makeIslandShape(island, settings)` draws an `IslandShape` once per island; `outlineRadius`, `outlineDistance`, `islandMask`, `topHeightAt` and `undersideDepthAt` evaluate it at island-local positions (metres in the XZ plane, relative to the rim anchor). The formulas and constants below are part of the save-compatibility contract (ADR 0003).

### Seeds

- From the island's shape seed (ADR 0013 Amendment 1), with `key(i) = mix64(shapeSeed + (i + 1) · kSplitMix64Gamma)`: `key(0)` seeds a `Pcg32` for the scalars, then outline noise `key(1)`, outline warp `key(2)`, ledge jitter `key(3)`, spurs `key(4)`, rock detail `key(5)`.
- From the island's terrain seed `deriveSeed(seed, RandomStream::Terrain, index, generatorVersion)`, keys the same way: `key(0)` for the scalars, relief warp `key(1)`, relief fBm `key(2)`, relief ridges `key(3)`. The relief can therefore change without moving outlines or undersides.
- Shape scalars in this order: the ellipticity `e = nextFloat(0, shape.ellipticity)`, then the long-axis direction: up to 16 points `(nextFloat(−1, 1), nextFloat(−1, 1))`, the first with squared length in [1/16, 1] normalized (a uniform direction without trigonometry), otherwise +X. Terrain scalar: `terraced = nextBool(elevation.plateauChance)`.

### Outline

- `k = 0.4 · shape.outlineRoughness`, long radius `a = radius / (1 + k)`, short radius `b = a · (1 − e)`.
- Along a unit direction d the ellipse radius is `1 / sqrt((d·axis / a)² + (d·axis⊥ / b)²)`; it is multiplied by `1 + k · n`, where n is fBm (3 octaves, lacunarity 2, persistence 0.5) of the outline key at `domainWarp(outline warp key, 0.8 · d, shape.outlineWarp)`. Reading the noise on a circle of radius 0.8 noise cells makes it a smooth periodic function of the direction without computing an angle, and makes the outline star-shaped: every ray from the anchor crosses it once. The result is at most `a · (1 + k) = radius` (clamped against rounding) and at least `0.24 · a`.
- The normalized distance `s = |p| / outlineRadius(p / |p|)` is 0 at the anchor, 1 on the outline.

### Top surface

- Mask: 1 for `s ≤ 0.75`, `fade((1 − s) / 0.25)` up to the outline, 0 on and outside it (`fade(t) = t³(t(6t − 15) + 10)`).
- Relief noise at `q = domainWarp(relief warp key, p · noise.frequency, noise.warpStrength, noise.fractal)`: hills `h = clamp(0.5 + 0.75 · fbm, 0, 1)`, ridges `r = ridgeNoise`, mixed as `(1 − ridgeWeight) · h + ridgeWeight · r`.
- Elevation `E = mask · (0.3 · (1 − s) + 0.7 · relief)`: a central mass plus hills and mountain crests.
- On a `terraced` island: with `elevation.terraceSteps = 0` a plateau, `E = min(E / 0.6, 1)` (flat top at full height); otherwise terraces, `steps(E, terraceSteps, 0.7)`, where `steps(v, n, f)` keeps each of n levels flat for its first share f and rises with `fade` over the rest, leaving 0 and 1 in place. The rises are the cliffs.
- `topHeightAt = topHeight · E`, so it stays in `[0, topHeight]`.

### Underside

- Profile `P = sqrt(1 − s) · (1 − 0.35 s)`: vertical at the rim, a broad body, narrowing to a point under the anchor.
- Ledge jitter: `P' = clamp(P + 0.8 · underside.roughness · j · P · (1 − P), 0, 1)` with j the fBm of the ledge key on the same circle as the outline; ledges move up and down around the island while the rim and the tip stay.
- Ledges: `steps(P', ledgeCount + 1, 0.6)` when `ledgeCount ≥ 1`: shelves of constant depth and steep faces between them.
- Spurs and rock detail, zero at the rim and the tip through the envelope `4 s (1 − s)`: plus `0.5 · protrusion · ridge³ · envelope` (ridge noise of the spur key at `p · 2.5 / radius`, hanging crests), plus `0.15 · roughness · fbm · envelope` (detail key at `p · 5 / radius`).
- Clamped to [0, 1] and multiplied by the island's `undersideDepth`. The deepest point is the tip under the anchor.

### Shared rules

- Both surfaces are 0 on and outside the outline, so they meet along the rim at the anchor's height. A non-finite position counts as outside; a zero or non-finite direction counts as +X.
- Arithmetic in double from the float inputs, noise through the M3.2 functions, square roots only (correctly rounded); results match bit for bit across platforms, and the golden tests use a tolerance of 1e-4 as the plan asks.

### Shape criteria

Measured per island over a corpus (30 seeds with the default settings, 360 islands), relative to the island's `undersideDepth`:

| Criterion | Measure | Threshold | Corpus minimum when fixed (100 seeds) |
|---|---|---|---|
| not a flat disc | mean depth over the footprint (41 × 41 grid) | ≥ 0.25 | 0.36 |
| not a flat disc | deepest point | ≥ 0.8 | 1.0 |
| varies around | largest spread of the depth over 64 directions on the rings s = 0.3, 0.5, 0.7 | ≥ 0.08 | 0.137 |
| not a cone along the depth | largest distance of the direction-averaged profile (41 steps) from the straight line between tip and rim | ≥ 0.08 | 0.125 |
| ledges | flat runs (depth change below 0.01 between steps of 1/40) per direction between s = 0.075 and 0.95 | ≥ 2 | 4.4 |
| not round | largest over smallest outline radius (128 directions) | ≥ 1.05 | 1.06 |

With ellipticity, outline roughness, underside roughness and protrusion all 0, an island is round and the same all around by design; its profile still differs from a cone and keeps its ledges.

## Consequences

- The constants (0.4, 0.8, 0.25, 0.3/0.7, 0.6, 0.35, 0.8, 2.5, 0.5, 5, 0.15) are code, not settings; tuning them changes worlds and needs a generator version bump once worlds are saved (M3.11).
- The underside is a height field below the footprint: no overhangs. Ledges, spurs and faces come from its depth; M3.6 meshes it in rings of constant s, which keeps faces crisp.
- The outline is star-shaped by construction, which keeps the rim one closed curve; bays that fold back on themselves are not possible.
- The look is checked for the first time in M3.8; the constants may be tuned there with an amendment.

## Amendment 1: island mesh (M3.6, 2026-11-13)

`buildIslandMesh(shape, resolution)` in `engine/worldgen/IslandMesh.hpp` turns a shape into two `MeshData` parts, `top` and `underside`, in island-local coordinates (anchor at the origin, +Y up). The construction is part of the save-compatibility contract.

- **Top: jittered grid with alternating diagonals.** n × n cells (n = `mesh.resolution`, clamped to 8–256) over the square [−1, 1]². Interior vertices move by up to ±0.25 cell per axis, drawn in row order from `Pcg32(key(6))` of the shape seed (a new key, so earlier keys are unchanged). A point (u, v) maps onto the island by radial normalization: it keeps its direction, and its normalized distance is `s = max(|u|, |v|)`, so every concentric square becomes a ring of constant s and the border becomes the rim; its position is `direction · s · outlineRadius(direction)`, its height `topHeightAt`. No trigonometry is needed. A smooth square-to-disk map was tried first and dropped: near the square's corners it moves the first interior vertex to within about h² of the rim, and jitter then folded cells. Diagonals alternate with (i + j) mod 2; a corner cell splits through its corner on the rim (whose two rim edges are nearly collinear); and where the map bends (along the square's diagonals) a jittered cell can become non-convex, so the other diagonal is taken when the alternating one would give a triangle that does not face up.
- **Rim.** The 4n border vertices, in order of increasing angle in the XZ plane, at height exactly 0. They are computed once and used by both parts, so the parts share their rim vertices bit for bit.
- **Underside: rings and a fan.** m = ceil(n / 2) rings at `s = 1 − k / m` (k = 0 is the rim) through the 4n rim directions, at depth `undersideDepthAt`; quads between rings split with diagonals alternating by (i + k) mod 2; the innermost ring closes with a fan to the tip `(0, −undersideDepth, 0)`.
- **Counts.** The top has 2n² triangles and the underside 4n(2m − 1), at most 6n² together (13 632 at the default n = 48).
- **Flat shading and colours.** Every triangle has its own three vertices with the face normal (`geometry::faceNormal`); indices run 0, 1, 2, …; vertex colours are white, to be replaced by palette colours (M3.7); there are no UVs.
- **Orientation and closure.** Both parts are height fields over the footprint: the top faces up and the underside down, both counter-clockwise seen from outside. Together they are closed (every edge appears once in each direction, V − E + F = 2).
- **Smallest triangle.** Every triangle has an area of at least `kMinIslandTriangleArea` = 1e−7 m². Measured: 0.0041 m² over the 72 islands of seeds 0–5 with the defaults, 1.06e−6 m² for a 4 m island at resolution 256 (the worst case of the limits).
- **Mesh hash.** `meshHash(mesh)` folds the vertex and index counts, every coordinate rounded to a multiple of `kMeshHashStep` = 1/1024 m (non-finite or beyond 2^62 steps: one fixed value), and every index with `mix64((h ^ x) + kSplitMix64Gamma)`. Golden hashes recorded on Windows are checked on Linux in CI (ADR 0003).
- `key(i)` of a seed now lives in the internal header `engine/src/worldgen/SubKeys.hpp`, shared by the shape and the mesh.

## Amendment 2: first look and the archipelago (M3.8, 2026-11-13)

- **Narrower mask falloff.** The first rendered world (M3.8) showed a wide, nearly flat brim around every island: the mask fell from 1 to 0 over the outer quarter of the outline (`s` from 0.75 to 1), so the top sloped down to the rim there, and the beach rule of ADR 0015 painted that band in sand; islands looked like plates with a hat brim. The falloff is now 0.08: the mask is 1 up to `s = 0.92` and falls with `fade((1 − s) / 0.08)` to 0 at the outline. The top keeps its height almost to the edge and ends in a short, steep drop that the palette paints as rock, above the underside's cliff, which reads as the edge of a floating island. Sand remains only where the relief itself is low near the rim. The underside, the outline and the shape criteria do not use the mask and are unchanged; so are the golden shape samples of M3.5, which all lie inside `s = 0.75`. The golden hashes of the top meshes (M3.6) changed and were recorded again.
- **Archipelago.** `generateArchipelago(settings)` in `engine/worldgen/Archipelago.hpp` runs placement (ADR 0013 Amendment 1), shape, mesh (Amendment 1) and painting (ADR 0015) for valid settings and returns the layout (with its warning) and one `GeneratedIsland` per placed island: its shape, its palette and top and underside appended into one painted, island-local `MeshData`. It is pure CPU data; island-preview uploads one mesh per island and draws it at the island's anchor.
