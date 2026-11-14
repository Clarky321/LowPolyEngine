# ADR 0016: Local island water — lakes, streams with ponds, waterfalls

- **Status:** Accepted
- **Date:** 2026-11-14
- **Related:** ADR 0003 (random streams, determinism), ADR 0011 (lighting), ADR 0013 (water settings, island volume), ADR 0014 (island shape and mesh), `docs/milestones/m3-floating-archipelago.md` M3.9, AGENTS.md §3 and §6

## Context

The floating world has no sea level and no global water plane: water exists only on the islands (M3.9). The plan asks for small lakes and ponds in basins of the top surface, streams that run downhill, and waterfalls over an island's edge where a stream reaches the rim, each from its own random stream, as stylized translucent surfaces drawn after the opaque geometry. The water must lie on or within its island, fill basins at or below the terrain around them, descend monotonically, start waterfalls at the rim, give valid meshes, repeat for a seed, and leave islands without suitable relief dry.

A first version traced streams by steepest descent on the top and let them end in the first pit. The relief of ADR 0014 has many small hollows, so most streams ended in one after a few metres: of 96 streams expected over 240 islands, 36 were found. Real streams fill such hollows and flow on; the generator now does the same.

## Decision

`engine/worldgen/IslandWater.hpp`: `generateIslandWater(shape, settings, islands)` returns an `IslandWater` with its lakes, the runs of its stream, its waterfall and one mesh, in island-local coordinates like the island's mesh. `generateArchipelago` (ADR 0014 Amendment 2) calls it for every island, with the layout's islands as obstacles. The procedure and constants below are part of the save-compatibility contract (ADR 0003).

### Seeds

- A new random stream `RandomStream::Water = 7`. The island's water seed is `deriveSeed(seed, Water, index, generatorVersion)`; with `key(i)` of ADR 0014, lakes draw from `Pcg32(key(0))`, the stream from `Pcg32(key(1))` and the waterfall from `Pcg32(key(2))`. Each kind of water body has its own sequence: changing the lake chance does not move streams, changing the waterfall chance moves neither.

### Sampling and basins

- The top is sampled on a grid of 48 × 48 cells over the island's bounding square: sample (i, j) at `(−r + i·h, −r + j·h)` with `h = 2r/48`, computed in double and rounded to float. A sample is inside if it lies within the bounding circle and `outlineDistance < 1`; its height is `topHeightAt`, 0 outside.
- **Flood.** A priority flood from all outside samples over the 8 neighbours (popped by lowest level, then lowest index) gives every sample its filled level `max(height, level of the sample it was reached from)` — the lowest level at which water on it could drain over the rim — and the sample it was reached from. The fill is unique; the order only decides ties of the drainage links.
- **Basins** are the 8-connected inside samples whose filled level is above their height. All samples of a basin have the same filled level, its spill level. A basin's bottom is its lowest sample (lowest index on ties). Water samples of different basins are never neighbours, so water bodies never share a grid cell.

### The stream and its ponds

- With `streamChance` the island gets a stream of width `radius · nextFloat(0.035, 0.06)`. Sixteen springs are drawn as `(nextFloat(−1, 1), nextFloat(−1, 1))` and snapped to the nearest sample; a spring must be inside, within `s ≤ 0.7`, at least 1 m above the rim and not in a basin. The springs are tried from the highest down (lowest index on ties, duplicates once).
- **Way down.** From the spring to the neighbour where the filled level drops most steeply (per distance, diagonals √2; first in neighbour order on ties); where it does not drop (in a basin, or on a flat) to the sample the flood reached it from. Every step lowers the filled level or moves along the flood's links towards the outside, so the way ends at an outside sample.
- A spring is taken if its way has at least 6 samples outside basins and passes through at most `maxLakesPerIsland` basins, each with all samples within `s ≤ 0.9`. Each of these basins becomes a **pond**: a lake filled to its spill level (`overflows`), where the stream flows on.
- **Runs.** The stream is stored as runs between its spring, its ponds and the rim. Each run's centre line passes through its samples at `height + 0.12 m`; it ends at the first sample of the next pond at that pond's level, or at the rim: the point where the last step crosses the outline (24 bisections on `outlineDistance`, the inner end), at height 0.12 m. The next run starts at the pond's outflow, the sample after it on the way, whose height equals the spill level. Heights never rise within a run.

### Lakes

- The other basins with all samples within `s ≤ 0.9`, a depth (spill level minus bottom) of at least 0.3 m and at least 4 samples are tried deepest first (lowest bottom index on ties), while the island has fewer than `maxLakesPerIsland` lakes and ponds: with `lakeChance` (`nextBool`) a lake is filled to `bottom + nextFloat(0.55, 0.95) · depth`. Its water samples are the 8-connected samples below that level around the bottom; a lake with fewer than 3 is left out.
- `maxLakesPerIsland` therefore counts lakes and ponds together. The stream's ponds come first in `IslandWater::lakes`, in the order the stream reaches them.

### Waterfall

- If the stream reaches the rim, with `waterfallChance`: `length = undersideDepth · nextFloat(0.8, 1.4)`, horizontal speed `nextFloat(0.8, 1.6)` m/s. It falls away from the anchor through the stream's mouth (`outward`), its top edge 0.15 m outside the rim at the mouth's height, 2.5 times as wide as the stream. The water leaves horizontally and falls freely: `drift = speed · sqrt(2 · length / 9.81)`, and the centre of the section at fraction f is `top + outward · drift · f − (0, length · f², 0)`, widening to 2.5 times its top width at the bottom.
- The waterfall is left out if any of its 6 segments passes through another island's volume (ADR 0013 Amendment 1): its vertical span overlaps the volume's and its centre line comes closer to the other anchor than that island's radius plus the segment's half width. Outward from a star-shaped outline it cannot meet its own island.

### Mesh and colours

- One mesh per island: the lakes, then the runs, then the waterfall; each body records its triangle range. Every triangle has its own vertices with the face normal (flat shading) and display-space colours with alpha below 1; triangles below 1e−6 m² are left out.
- **Lakes**: every grid cell with a water sample among its corners, two triangles with alternating diagonals at the lake's level, facing up. The corners that are not water lie at or above the level, so the surface ends under the shore. Each triangle is coloured by the depth below its centre, from a light, clearer turquoise (0.47, 0.82, 0.83, alpha 0.62) to a deep blue (0.13, 0.43, 0.62, alpha 0.86) at 2.5 m.
- **Runs**: a ribbon along the centre line, facing up, sideways by half the width at each point (perpendicular to the line through its neighbours), colour (0.52, 0.84, 0.88, alpha 0.82).
- **Waterfall**: both sides of a ribbon of 6 segments, so it shows from outside and from below the island, colour (0.80, 0.93, 0.98) with alpha 0.92 · (1 − f²), fading out at the bottom.
- island-preview draws all water after the opaque islands, with alpha blending and an unlit white material (its colours as generated), island by island from back to front.

## Consequences

- Lakes and ponds are small: the relief of ADR 0014 makes hollows of a few metres, and on a 48-cell grid most lakes cover 3 to 30 samples. The world reads as islands with pools, streams and waterfalls rather than islands with large lakes; larger lakes would need terrain shaped for them (out of scope for M3).
- Water follows the sampled grid, not the island mesh: a lake surface may end a little inside or outside the visible shore, and a stream may dip under the mesh between samples; the depth test hides what lies under the rock.
- Translucent water writes depth and is sorted only per island; overlapping translucent surfaces may show sorting artifacts (plan, Known limitations).
- The constants (48, 0.9, 0.3, 4, 3, 0.55–0.95, 16, 0.7, 1 m, 6, 0.035–0.06, 0.12 m, 24, 0.8–1.4, 0.8–1.6 m/s, 9.81, 6, 2.5, 0.15 m, the colours) are code, not settings; tuning them changes worlds and needs a generator version bump once worlds are saved (M3.11).
- An independent implementation reproduces the lakes, runs and waterfalls of seeds 42, 1 and 7 exactly; golden values for seed 42 and the water mesh hashes are checked on Windows and Linux.
