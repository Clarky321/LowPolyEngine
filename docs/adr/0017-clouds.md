# ADR 0017: Clouds — low-poly clusters between the islands and their drift

- **Status:** Accepted
- **Date:** 2026-11-14
- **Related:** ADR 0003 (random streams, determinism), ADR 0013 (cloud settings, island volume), ADR 0014 (mesh hash), ADR 0016 (water, drawn after the islands), `docs/milestones/m3-floating-archipelago.md` M3.10, AGENTS.md §6

## Context

M3.10 adds minimalist low-poly clouds in the air around and between the floating islands: clusters at different altitudes, with different sizes and densities, from their own random stream, keeping a clearance from the islands, and drifting slowly along a wind from the settings. The drift is a display effect: the generated data is the initial placement, which must repeat exactly for a seed; the position at a time is a formula of that placement and the time, wrapped inside the world bounds. The art direction asks for stylized, colour-led clouds, not photorealistic ones (AGENTS.md §6).

## Decision

`engine/worldgen/Clouds.hpp`: `generateClouds(settings, islands)` returns a `CloudField` (the placed clusters, the requested count and a warning); `cloudDrift(cluster, settings, seconds)` gives where a cluster is shown at a time. `generateArchipelago` (ADR 0014 Amendment 2) places the clouds after the islands, with the layout as obstacles. The procedure and constants below are part of the save-compatibility contract (ADR 0003); the drift is not generated data.

### Placement

- Cluster k (0 to `clouds.clusterCount − 1`) draws from `Pcg32(deriveSeed(seed, RandomStream::Clouds, k, generatorVersion))`, up to 100 attempts of exactly four draws each: radius `nextFloat(size.min, size.max)`, x `nextFloat(bounds.min.x + r, bounds.max.x − r)`, z likewise, base altitude y `nextFloat(altitude.min, altitude.max)`.
- **Cluster volume:** the vertical cylinder of radius r around the anchor (the centre of the flat base) from y up to `y + 0.65 r`. An attempt fits if the bounds are at least 2r wide in X and Z and the volume's top is at most `bounds.max.y`; it keeps the clearance from an island volume (ADR 0013 Amendment 1) if the horizontal distance of the anchors is at least `r + island radius + islandClearance`, or the vertical gap between the volumes is at least `islandClearance` — the rule islands use among themselves. The first attempt that fits and keeps the clearance from every island is taken; a cluster without one is left out, and the warning names how many of the requested clusters were placed.

### Shape

- From `Pcg32(key(0))` of the cluster's seed (ADR 0014's `key(i)`): the density `clamp(nextFloat(density − 0.25, density + 0.25), 0, 1)` and `1 + round(8 · density)` puffs (1 to 9); then the cluster's long axis, a uniform direction without trigonometry (up to 16 points of the square, the first in the ring between radius 1/4 and 1, normalized; otherwise +X).
- **Puffs.** The first is the core: radius `r · nextFloat(0.45, 0.55)` at the centre. Each other one has radius `r · nextFloat(0.25, 0.45)` and a point (u, v) of the unit disc (up to 16 points of the square, the first inside; otherwise the centre); with `reach = r − 1.15 · radius` its centre lies at `along = u · reach` on the long axis and `across = v · reach · 0.55` across it. Every puff's centre lies `0.3 · radius` above the base.
- **Mesh.** Each puff is a unit icosahedron subdivided once (42 vertices, 80 faces, counter-clockwise from outside; midpoints pushed onto the sphere, created in face order), computed in double. Each vertex lies at `centre + direction · radius · j` with its own `j = nextFloat(0.88, 1.12)` (42 draws per puff, in vertex order), the height component scaled by 0.75, so a puff is three quarters as high as wide; every vertex below the base is moved up onto it, which cuts the puffs flat underneath. With these bounds every vertex lies inside the cluster volume. Every face has its own vertices with the face normal (flat shading); faces below 1e−6 m² are left out (none were in the 40 clusters of seeds 42 and 1).
- **Colours** from data, by the facing of a face: white (1, 1, 1) where it faces up, a lavender blue (0.76, 0.78, 0.92) where it faces down, linear in `0.5 + 0.5 · normal.y` in between; alpha 1. Clouds are opaque, so they need no sorting.

### Drift

- The wind blows towards `windDirectionDegrees` (0 is +X, 90 is +Z) at `windSpeed` m/s. At time t the anchor moves by `(cos θ, 0, sin θ) · speed · t`; X and Z each wrap into `[bounds.min, bounds.max)` (in double, with `fmod`). The scale is `smoothstep(0, 1, d / r)` with d the distance from the nearer edge in X or Z: a cloud shrinks away within one radius of the edge, where it wraps around, and appears on the other side growing back. Placement keeps every cluster at least r from the edges, so at time 0 it is at its anchor with scale 1.
- Trigonometry is used only for the drift, which is not generated data.

### Drawing (island-preview)

- One mesh per cluster, drawn opaque after the islands with the unlit material of the water (ADR 0016), at `translate(drift.position) · scale(drift.scale)`; a cloud below a thousandth of its size is skipped. The log line and the overlay name the number of clusters.

## Consequences

- Clearance is a property of the placement. While drifting, a cloud can pass an island; the wind is a display effect, and keeping every cloud's whole drift path clear would leave clouds only above and below the islands.
- Unlit, facing-coloured clouds stay light on every side, like the pastel clouds of the art direction; they do not take the sun's direction. A lit version made them look like grey boulders on the first look.
- A cluster has at most 720 triangles; 20 clusters add about 8 000 to a world of about 164 000.
- The constants (100, 0.65, 0.25, 8, 0.45–0.55, 0.25–0.45, 1.15, 0.55, 0.3, 0.88–1.12, 0.75, 16, the colours) are code, not settings; tuning them changes worlds and needs a generator version bump once worlds are saved (M3.11).
- An independent implementation reproduces the clusters, puffs and mesh hashes of seeds 42 and 1 exactly; the golden values of seed 42 are checked on Windows and Linux.
