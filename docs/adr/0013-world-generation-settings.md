# ADR 0013: World generation settings

- **Status:** Accepted; amended for M3.4 (Amendment 1), M3.9 (Amendment 2) and M3.10 (Amendment 3)
- **Date:** 2026-11-13
- **Related:** ADR 0003 (generator version, determinism), ADR 0004 (JSON envelope, validation after loading), `docs/milestones/m3-floating-archipelago.md` M3.3, M3.11 and M3.12, `docs/ENGINE_PLAN.md` §6

## Context

M3 generates a floating archipelago from a seed and a set of parameters. The generator steps (M3.4–M3.10), the JSON loader (M3.11) and the WorldGen panel (M3.12) need one description of those parameters: the same fields, defaults and rules, so that neither a hand-edited file nor a slider can produce input the generator does not expect (ADR 0004, rule 8). The world has no sea level: islands float at different altitudes, and water exists only on them.

## Decision

1. **One plain value type**, `engine::worldgen::WorldGenSettings` in `engine/worldgen/WorldGenSettings.hpp`, grouped like the JSON of ADR 0004 (`"islands": { "count": … }`): `seed`, `generatorVersion`, `bounds`, `islands`, `shape`, `elevation`, `underside`, `noise`, `water`, `clouds`, `mesh`. Problems name a field by its member path, such as `islands.altitude.max`; the JSON loader will use the same paths.
2. **Units.** Lengths in metres, +Y up. Wind direction in degrees in the XZ plane (0 is +X, 90 is +Z), because settings are authored data. Noise frequency in noise cells per metre and warp strengths in noise cells (ADR 0003 Amendment 2). Height and depth of an island as fractions of its radius, so shapes scale with island size.
3. **Island anchor.** An island is placed by the centre of its rim, where the top surface meets the underside. Its outline stays inside a circle of its radius; its top rises at most `elevation.maxHeightRatio · radius` above the rim, its underside reaches at most `underside.depthRatio.max · radius` below it. Placement (M3.4) builds island volumes from these numbers.
4. **Defaults** are the member initializers; `WorldGenSettings{}` is valid. **Ranges** are inclusive and public constants in `engine::worldgen::limits`, so tests and the panel use the same numbers.
5. **Two functions, one policy:**
   - `validate(settings)` returns the first problem as a `core::Error`, or nothing. It rejects a generator version other than `kGeneratorVersion`, every numeric field outside its range (NaN and infinities are outside), and the combinations of decision 6. The order is fixed: version, the fields in the order of declaration, then the combinations. Each message starts with the field path and gives the value, for example `islands.count is 0, outside its range 1 to 64`. Floats in messages use plain decimal notation (`{:g}`).
   - `clampToLimits(settings)` moves every numeric field into its range; NaN becomes the field's default. It leaves the seed, the generator version, the water switch and all combinations unchanged.
   - Combinations are never repaired silently: swapping an inverted range or moving one of its ends would change the world without anyone deciding it. The caller decides: the loader (M3.11) is expected to reject a file that does not validate, and the panel (M3.12) to clamp single values and show `validate`'s message for combinations.
6. **Combinations:** every axis of `bounds` spans at least `limits::kMinWorldExtent` (20 m); the diagonal of `bounds` is at most `limits::kMaxWorldDiagonal` (1000 m, the camera's far plane); every `FloatRange` has `min <= max`; the largest island fits the bounds horizontally (`2 · islands.radius.max` not wider than the narrower of X and Z); at the highest altitude its top and at the lowest altitude its deepest underside stay inside the Y bounds; `islands.minHorizontalSpacing` is not larger than the narrower horizontal extent and `islands.minVerticalSpacing` not larger than the Y extent; cloud altitudes lie inside the Y bounds. Whether the requested number of islands actually fits is decided by placement (M3.4), which has a documented policy for it.
7. **Generator version.** `kGeneratorVersion` is 1 and only that version validates: this build cannot generate the output of another one. It is incremented whenever the same settings would produce a different world (ADR 0003); converting older settings files is the job of the loader's migrations (ADR 0004, rule 5).

### Fields

| Path | Default | Range | Meaning |
|---|---|---|---|
| `seed` | 1 | any 64-bit value | master seed |
| `generatorVersion` | 1 | exactly `kGeneratorVersion` | generator that the settings are for |
| `bounds.min.x/y/z` | −260, −80, −260 | −500 to 500 | lower corner of the box holding all islands and clouds |
| `bounds.max.x/y/z` | 260, 160, 260 | −500 to 500 | upper corner |
| `islands.count` | 12 | 1 to 64 | islands requested |
| `islands.altitude.min/max` | −20, 110 | −500 to 500 | rim height of the islands |
| `islands.radius.min/max` | 16, 55 | 4 to 150 | radius of an island's bounding circle |
| `islands.minHorizontalSpacing` | 18 | 0 to 200 | horizontal gap two islands keep, unless they keep the vertical one |
| `islands.minVerticalSpacing` | 12 | 0 to 200 | vertical gap two islands keep, unless they keep the horizontal one |
| `shape.ellipticity` | 0.3 | 0 to 0.6 | largest flattening of the outline (short axis = (1 − e) · long axis) |
| `shape.outlineRoughness` | 0.35 | 0 to 1 | strength of the outline noise |
| `shape.outlineWarp` | 0.75 | 0 to 64 | domain warp of the outline noise, noise cells |
| `elevation.maxHeightRatio` | 0.4 | 0.05 to 1 | highest point of the top above the rim, fraction of the radius |
| `elevation.ridgeWeight` | 0.4 | 0 to 1 | share of ridge noise (mountain crests) |
| `elevation.plateauChance` | 0.3 | 0 to 1 | probability of plateaus and terraces on an island |
| `elevation.terraceSteps` | 3 | 0 to 8 | terrace levels on such an island |
| `underside.depthRatio.min/max` | 0.6, 1.0 | 0.2 to 2 | depth below the rim, fraction of the radius |
| `underside.ledgeCount` | 3 | 0 to 6 | steps (ledges) in the depth profile |
| `underside.protrusion` | 0.4 | 0 to 1 | strength of spurs and protrusions |
| `underside.roughness` | 0.5 | 0 to 1 | irregularity of the rock faces |
| `noise.frequency` | 0.045 | 0.001 to 1 | noise cells per metre |
| `noise.fractal.octaves` | 4 | 1 to 12 | fractal noise layers |
| `noise.fractal.lacunarity` | 2 | 1 to 4 | frequency factor between octaves |
| `noise.fractal.persistence` | 0.5 | 0 to 1 | amplitude factor between octaves |
| `noise.warpStrength` | 0.6 | 0 to 64 | domain warp of the relief, noise cells |
| `water.enabled` | true | true or false | local water at all |
| `water.lakeChance` | 0.5 | 0 to 1 | probability that a suitable basin holds a lake or pond |
| `water.maxLakesPerIsland` | 2 | 0 to 4 | lakes and ponds per island |
| `water.streamChance` | 0.4 | 0 to 1 | probability of a stream on an island |
| `water.waterfallChance` | 0.6 | 0 to 1 | probability that a stream reaching the rim falls over it |
| `clouds.clusterCount` | 20 | 0 to 128 | cloud clusters |
| `clouds.altitude.min/max` | −60, 150 | −500 to 500 | height of the cloud clusters |
| `clouds.size.min/max` | 8, 26 | 1 to 100 | radius of a cluster |
| `clouds.density` | 0.55 | 0 to 1 | how densely a cluster is filled with shapes |
| `clouds.windDirectionDegrees` | 30 | 0 to 360 | direction the wind blows towards |
| `clouds.windSpeed` | 1.5 | 0 to 20 | drift in m/s (a display effect, not generated data) |
| `clouds.islandClearance` | 6 | 0 to 50 | smallest gap between a cloud and an island volume |
| `mesh.resolution` | 48 | 8 to 256 | grid cells across an island's diameter |

The noise ranges are those of the noise functions (`kMaxNoiseOctaves`, `kMaxNoiseLacunarity`, `kMaxWarpStrength`).

## Consequences

- The shape, water and cloud fields are defined before the steps that use them (M3.5, M3.9, M3.10), because the plan puts all settings into M3.3. Those steps may refine them, with an amendment to this ADR, while settings are not yet saved to files; once M3.11 writes files, a change needs the format version and migrations of ADR 0004.
- A range admits values that only some combinations accept (for example `bounds.min.x = 500` can never have room above it); per-field limits stay simple, and `validate` explains the combination.
- `validate` reports one problem at a time; fixing it may reveal the next. That is enough for the loader's error message and the panel's status line.
- No feasibility check of the island count: it depends on the random sizes, so placement handles it.

## Amendment 1: island placement (M3.4, 2026-11-13)

M3.4 turns the `islands` settings into a layout: `placeIslands(settings)` in `engine/worldgen/IslandLayout.hpp` returns an `IslandLayout` (the placed islands, the requested count and a warning) or `validate`'s error for invalid settings. The procedure below is part of the save-compatibility contract (ADR 0003): changing it changes worlds and needs a generator version bump.

- **Island volume.** A placed island has an index (the number of the requested island, 0 to `islands.count − 1`), its rim anchor, a radius, `topHeight = elevation.maxHeightRatio · radius` and `undersideDepth = d · radius` with d drawn from `underside.depthRatio`. Its volume is the vertical cylinder of that radius from `anchor.y − undersideDepth` to `anchor.y + topHeight`.
- **Spacing.** Two islands are far enough apart when the horizontal distance between their cylinders is at least `islands.minHorizontalSpacing` or the gap between their height intervals is at least `islands.minVerticalSpacing`. Either keeps their geometry apart.
- **Streams.** Requested island k draws from its own `Pcg32(deriveSeed(seed, RandomStream::IslandLayout, k, generatorVersion))`, so its candidates do not depend on how many attempts earlier islands needed; its shape seed is `deriveSeed(seed, RandomStream::IslandShape, k, generatorVersion)`. A layout with more islands therefore starts with the islands of a layout with fewer, as long as they all fit.
- **Candidates.** Each attempt draws, in this order, the radius from `islands.radius`, the depth ratio from `underside.depthRatio`, x from `[bounds.min.x + radius, bounds.max.x − radius)`, z likewise and the rim altitude from `islands.altitude`, all with `nextFloat` (one output each). Decision 6 guarantees that every such candidate lies inside the bounds, vertically too. The first candidate that keeps the spacing to every island placed before it is taken.
- **When islands do not fit.** After `kPlacementAttemptsPerIsland` (100) attempts without a fitting candidate, the island is left out and the next one is tried; the layout keeps the indices of the placed islands, and its warning reads, for example, `placed 8 of 40 islands: 32 found no position that keeps the spacing in 100 attempts each`. Placement is pure, so the caller logs the warning. The first island always fits, so a layout is never empty, and the work is bounded by `islands.count × 100` attempts.
- **Arithmetic.** Distances are compared squared in double (no square root); all values come from correctly rounded IEEE operations, so layouts match bit for bit across platforms (golden tests).

## Amendment 2: local water (M3.9, 2026-11-14)

The water fields keep their names, defaults and ranges; ADR 0016 defines what they control:

- `water.enabled`: no lakes, streams or waterfalls at all when false.
- `water.lakeChance`: the probability that each suitable basin, deepest first, is filled as a lake.
- `water.maxLakesPerIsland`: lakes and ponds together. A stream that would need more ponds (filled hollows on its way) is not taken; lakes are placed only while there is room.
- `water.streamChance`: the probability of a stream on an island; it reaches the rim through its ponds.
- `water.waterfallChance`: the probability that the stream falls over the rim as a waterfall, unless the fall would pass through another island.

## Amendment 3: clouds (M3.10, 2026-11-14)

The cloud fields keep their names, defaults and ranges; ADR 0017 defines what they control:

- `clouds.clusterCount`: clusters requested; one that finds no room is left out, with a warning.
- `clouds.altitude`: the height of a cluster's flat base. Its top lies 0.65 times its radius higher and must stay within `bounds.max.y`.
- `clouds.size`: a cluster's horizontal radius.
- `clouds.density`: the mean density; each cluster varies it by up to ±0.25 (within 0 to 1) and gets 1 + round(8 × density) puffs.
- `clouds.windDirectionDegrees`, `clouds.windSpeed`: the drift, a display effect.
- `clouds.islandClearance`: the gap a cluster's volume keeps from every island volume, horizontally or vertically, when placed.
