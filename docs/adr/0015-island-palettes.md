# ADR 0015: Island palettes

- **Status:** Accepted
- **Date:** 2026-11-13
- **Related:** ADR 0003 (random streams), ADR 0009 (display-space colours), ADR 0014 (island shape and mesh), `docs/milestones/m3-floating-archipelago.md` M3.7 and M3.8, AGENTS.md §6

## Context

M3.6 builds white island meshes. M3.7 colours them from palette data: grass, soil, sand, rock and snow on the top, stone on the underside, chosen by elevation, slope and position, with a palette per island so that neighbours look different. Colours must not live in shader code (AGENTS.md §6) and must repeat for a seed.

## Decision

`engine/worldgen/IslandPalette.hpp`.

### Palette data

- `IslandPalette` holds seven display-space RGB colours (ADR 0009): `grass`, `soil`, `sand`, `rock`, `snow`, and for the underside `stone` (below the rim) and `deepStone` (at the tip). `validate()` names the first component that is not finite or outside [0, 1].
- Six built-in palettes, in this order (`builtInPalettes()`): Meadow, Autumn, Teal moss, Sandstone, Lavender dusk, Alpine. They are pastel and colour-led in the spirit of the art direction; values are in `IslandPalette.cpp`. They are code data for M3; loading palettes from files is not part of M3 (settings files in M3.11 do not include them).

### Choice per island

- A new random stream, `RandomStream::IslandPalette = 6` (the next free value; local water will take the one after it). Island k of the layout draws from `Pcg32(deriveSeed(seed, IslandPalette, index, generatorVersion))`, always in this order: base palette `nextBounded(6)`, alternative `nextBounded(5)`, brightness `nextFloat(0.92, 1.08)`, tint per channel `nextFloat(−0.05, 0.05)` for r, g, b.
- Neighbours: if the base palette equals that of the nearest island placed before this one (3D distance between anchors, first on ties), the base becomes `(base + 1 + alternative) mod 6`, which always differs.
- Every colour of the base palette becomes `clamp(colour · brightness · (1 + tint), 0, 1)`, so every island has colours of its own even when it shares a base palette with another.
- `choosePalettes(layout, settings)` returns the base index and the tinted palette per island, in layout order. It depends only on the seed, the version and the island anchors; shape settings do not change it.

### Painting

`paintIsland(mesh, shape, palette)` writes one colour per face (all three vertices, alpha 1), keeping the faces crisp:

- **Top:** for the face centre, `elevation = y / topHeight`, `normalY` = the face normal's y, `rimDistance = outlineDistance(centre)`. Rules in this order: rock if `normalY < 0.7` (steeper than about 46°); snow if `elevation ≥ 0.82`; sand if `rimDistance ≥ 0.88` and `elevation < 0.1` (beaches along the rim); soil if `normalY < 0.86` (slopes steeper than about 31°); otherwise grass. `classifyTop` exposes the rule; the thresholds are constants in the header.
- **Underside:** four strata by the face centre's depth fraction d (depth below the rim / undersideDepth): stratum `min(floor(4d), 3)`, coloured from `stone` (0) to `deepStone` (3) in equal steps. The strata read as rock layers and darken the mass towards the tip.

## Consequences

- Shaders stay free of palettes; the renderer only multiplies vertex colours with the material (M2).
- Snow is relative to each island's own top height, so tall and small islands both get white peaks; plateau islands at full height get snowy plateaus. Whether that reads well is checked in M3.8; thresholds and palettes may be tuned there with an amendment.
- The tint makes colours unique per island but keeps the base palette's character; six palettes with the neighbour rule keep at least two, and in practice three or more, looks per world (tested over 100 seeds).
- Adding palettes changes `nextBounded(6)` and therefore the colours of existing worlds; it needs a generator version bump once worlds are saved (M3.11).
