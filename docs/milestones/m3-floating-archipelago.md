# M3 — Procedural floating sky archipelago

- **Branch:** `milestone/m3-floating-archipelago`, created from `main` at `6d61fba` (M2 merged through pull request #3).
- **Roadmap entry:** `docs/ENGINE_PLAN.md` §10, M3; generator design in §6.
- **Workflow:** `docs/DEVELOPMENT_WORKFLOW.md`. One task implements exactly one sub-milestone below, then stops and reports.

## Goal and exit criterion

M2 draws lit, flat-shaded, partly textured meshes. M3 generates the first real content from a seed: an archipelago of independent floating islands in open three-dimensional sky, with local water on some islands and low-poly clouds around and between them. There is no world ocean and no global water plane.

**Exit** (from `docs/ENGINE_PLAN.md`): several visibly different archipelagos are generated (islands at different heights, with different sizes, shapes and palettes, rocky undersides, local water where the generator places it, clouds between them); the same seed repeats; generation tests pass; the free-fly camera moves freely through the whole 3D space.

## Concept

- The world is a set of independent floating islands placed in X/Y/Z inside explicit world bounds, at different altitudes, with free space between them.
- Each island is one procedurally generated object with:
  - an uneven top surface with hills, mountains, cliffs and plateaus;
  - a voluminous rocky underside with ledges, protrusions and pronounced faces — never a flat disc or a plain inverted cone;
  - its own color palette;
  - optionally lakes and ponds, streams, and waterfalls over its edge.
- Cloud clusters float around and between the islands at different altitudes and drift slowly with the wind.
- Variety comes from geometry, relief, altitude, biomes, palettes and local water — not from entities, vegetation or buildings (M4 and later).

## Art direction

- Stylized low-poly 3D: flat shading, vertex colors, crisp geometric faces, expressive palettes, simple but readable forms, minimalist stylized clouds and stylized water surfaces (AGENTS.md §6).
- Main visual reference: *AER: Memories of Old*. Further references for atmosphere, color and composition: *Journey*, *ABZÛ*, *The Witness*. They guide mood only; no assets, characters or level designs are copied.
- Not in scope: photorealism, heavy PBR, photorealistic clouds, high-resolution textures.

## Status

| ID | Title | Status |
|---|---|---|
| M3.1 | Deterministic randomness | planned |
| M3.2 | Noise | planned |
| M3.3 | World generation settings | planned |
| M3.4 | Island placement in 3D | planned |
| M3.5 | Island shape: top surface and underside | planned |
| M3.6 | Island mesh | planned |
| M3.7 | Island palettes | planned |
| M3.8 | Floating world preview | planned |
| M3.9 | Local island water | planned |
| M3.10 | Clouds | planned |
| M3.11 | Settings as versioned JSON | planned |
| M3.12 | WorldGen panel with Dear ImGui | planned |
| M3.13 | Final verification and pull request | planned |

Status values: `planned`; `in progress` (set when work starts and committed with the change); `done` (set by a separate `docs(milestones): mark M3.x done (M3.x)` commit after the change is pushed and its CI is green, `docs/DEVELOPMENT_WORKFLOW.md` §4). `git log --grep "(M3.x)"` finds the commits.

## Rules for all of M3

- WorldGen is a pure CPU module in `engine/worldgen` (ADR 0003): no OpenGL, SDL or ImGui dependency. It produces `geometry::MeshData` and plain world data; the renderer only draws them.
- Determinism follows ADR 0003: project-owned SplitMix64/PCG32 and distributions, no `std::hash`, standard distributions, clocks or unordered iteration order; no fast-math, no FMA contraction in worldgen sources; `generatorVersion` is part of every derived seed. Every new random use gets its own `RandomStream` value, so adding a feature does not reshuffle existing output. Time (cloud drift, water motion) is a display effect and never part of generated data.
- There is no world sea level and no global water plane. Water exists only as local bodies on islands (M3.9).
- Flat shading comes from per-face normals on vertices that faces do not share (ENGINE_PLAN §5). Colors come from palette data, not from shader code (AGENTS.md §6).
- M3 draws with the renderer API as of M2 (meshes, materials, textures, one directional light, view/depth/blend state). The sky background, translucent water and clouds use that API. If a step turns out to need a renderer change, the plan is changed first in a separate `docs(milestones)` commit approved by the user.
- World bounds stay inside the camera's far plane (1000 m) with room for the camera to fly around them.
- Every generated mesh passes `geometry::validate()` and the invariants of `docs/TESTING_AND_CI.md` §2 (finite values, indices in range, non-degenerate triangles, normalized normals, bounds).
- New dependencies only in the steps that name them: `nlohmann_json` (M3.11, decided in ADR 0004) and Dear ImGui (M3.12). Each is recorded in `docs/references/THIRD_PARTY_NOTICES.md` and `conanfile.py` with a pinned version.
- Decisions about dependencies, public APIs and file formats get an ADR in the sub-milestone that makes them (AGENTS.md §10), numbered when written.
- Out of scope for M3: ECS or an entity system, game scenes, vegetation, characters, objects and buildings (M4 and later), the full editor (M5).

## Sub-milestones

### M3.1 — Deterministic randomness

**Goal:** the reproducible random foundation of ADR 0003.

**Scope:**
- New module `engine/worldgen` (library code only, no rendering).
- `mix64` (SplitMix64 finalizer), `deriveSeed(masterSeed, stream, index, generatorVersion)`, `enum class RandomStream` with fixed values, `Pcg32` (XSH-RR, 64-bit state) as a value type, `nextFloat01()` and `nextBounded(n)` (Lemire with rejection), ranges and booleans built on them.
- `-ffp-contract=off` for worldgen sources on GCC/Clang; no fast-math anywhere.

**Tests (headless):** exact golden values for `mix64`, `deriveSeed`, the first outputs of `Pcg32` for fixed seeds and `nextBounded`, identical on Windows and Linux in CI; `nextFloat01` stays in [0, 1); `nextBounded` covers its range without bias on a large sample; different streams and indices give different seeds.

**Done when:** the golden tests pass in CI on all four jobs.

**Visual check:** none.

### M3.2 — Noise

**Goal:** deterministic noise for shapes and relief.

**Scope:**
- 2D gradient noise with a polynomial fade curve, seeded through M3.1 (no global permutation table).
- fBm (octaves, persistence, lacunarity; amplitudes by repeated multiplication instead of `std::pow`), ridge noise, domain warp.
- Pure functions of position, seed and parameters.

**Tests (headless):** output ranges for every function; continuity (small steps give small changes); bitwise repeatability in the same binary; stored samples compared with a tolerance across platforms; parameter edge cases (one octave, zero warp).

**Done when:** all noise functions are covered by range, repeatability and golden-sample tests.

**Visual check:** none.

### M3.3 — World generation settings

**Goal:** one versioned description of a world.

**Scope:**
- `WorldGenSettings`: seed, generator version, island count, world bounds X/Y/Z, vertical altitude range, island size range, minimum horizontal and vertical spacing, shape parameters, elevation parameters, noise parameters, local water settings, cloud settings, mesh resolution.
- Documented defaults and a validation and clamping policy per field (reject or clamp, as `docs/ENGINE_PLAN.md` §6 asks), shared later by the JSON loader (M3.11) and the UI (M3.12).
- No world sea level: the concept is removed.

**Tests (headless):** defaults are valid; every field below, at and above its limits; inconsistent combinations (altitude range outside the Y bounds, size range that cannot fit the bounds, spacing larger than the world) are rejected with a message naming the field.

**Done when:** every field has a default, a documented range and a test.

**Visual check:** none.

### M3.4 — Island placement in 3D

**Goal:** deterministic positions, sizes and seeds for all islands.

**Scope:**
- Place island centers in X/Y/Z inside the world bounds and the altitude range, with sizes drawn from the size range.
- Each island gets a bounding volume (horizontal radius, top height, underside depth). Two islands must keep the minimum horizontal spacing between their volumes or the minimum vertical spacing between them, so their geometry cannot intersect.
- Bounded attempts in a fixed order; if the requested count does not fit, a documented policy applies (fewer islands and a warning, never an endless loop).
- Every island gets its own derived seed (`RandomStream::IslandLayout` for placement, per-island streams for its shape).

**Tests (headless):** determinism (same settings, same layout bitwise); all islands inside the bounds; no pair violates the clearance rule; altitudes and sizes vary over a seed corpus; different seeds give different layouts; per-island seeds are distinct; impossible settings follow the documented policy.

**Done when:** placement passes the tests above for a corpus of seeds.

**Visual check:** none.

### M3.5 — Island shape: top surface and underside

**Goal:** the shape of one island as height data, top and bottom.

**Scope:**
- Top surface: island mask (normalized elliptical distance, smooth falloff, low-frequency shape noise and domain warp for an irregular outline), elevation from the mask, fBm and ridge noise, optional plateaus and terraces, giving hills, mountains, cliffs and plateaus. Heights stay inside documented bounds.
- Underside: a rocky mass below the outline whose depth and profile vary around the island, with ledges (steps in depth), protrusions and spurs, and pronounced faces. It is irregular low-poly rock, never a flat disc or a plain inverted cone; measurable criteria (variation of the profile around the island and along the depth) are fixed in this step.
- Both parts meet at the island's rim.

**Tests (headless):** height and depth bounds; mask is zero outside the footprint; the rim is shared by top and underside; the not-a-disc/not-a-cone criteria hold over a seed corpus; determinism and golden samples with tolerance.

**Done when:** top and underside data meet their bounds and shape criteria for a corpus of seeds.

**Visual check:** none (data only; first shown in M3.8).

### M3.6 — Island mesh

**Goal:** a closed, flat-shaded mesh per island.

**Scope:**
- Top surface from a jittered grid with alternating diagonals inside the outline (ENGINE_PLAN §6); underside from rings or strips following its profile; both as `MeshData` with per-face normals, indices and white or palette-ready vertex colors.
- The two parts meet along the rim without gaps; the island is closed when seen from any direction.
- A stable mesh hash of positions quantized to a documented grid (ADR 0003) for repeatability tests; triangle count bounded by the mesh resolution.

**Tests (headless):** `geometry::validate()` and the §2 invariants for both parts; outward-facing winding (counter-clockwise seen from outside) on top and underside; no gaps along the rim; triangle area above a documented epsilon; bounds; same seed gives the same hash, and the golden hash matches on Windows and Linux in CI.

**Done when:** every island of a seed corpus produces valid, closed meshes with stable hashes.

**Visual check:** none (first shown in M3.8).

### M3.7 — Island palettes

**Goal:** colors from data, different per island.

**Scope:**
- Palette data with separate colors for grass, soil, sand, rock surfaces, snow and the stone underside; selection by elevation, slope and position on the island, with documented thresholds.
- Each island picks or varies a palette deterministically, so neighbors look different.
- Colors are written as vertex colors; shaders contain no palette.

**Tests (headless):** palette validation (finite components in [0, 1]); thresholds pick the expected entries; the underside uses the stone palette; different islands of a seed corpus get different palettes; determinism.

**Done when:** every vertex of every island has a palette color and the variety test passes.

**Visual check:** none (first shown in M3.8).

### M3.8 — Floating world preview

**Goal:** see the archipelago and fly through it.

**Scope:**
- island-preview shows the generated archipelago instead of the test scene; the test scene stays available behind a command-line flag.
- A simple stylized sky background (a vertical color gradient from data, drawn with the existing renderer before the scene) and a sunlight that suits floating islands.
- Islands at different altitudes with free space between them; a starting camera that overlooks the archipelago; the free-fly camera moves through the whole 3D space.
- Generation time and island/triangle counts in the log and the debug overlay.

**Tests:** GPU smoke runs of the generated world without OpenGL errors and without logged errors; the test-scene flag still works; headless tests stay green.

**Done when:** the archipelago renders from a default seed and the smoke tests pass.

**Visual check:** screenshot `docs/screenshots/m3/` (first image).

### M3.9 — Local island water

**Goal:** water that belongs to islands only.

**Scope:**
- Small lakes and ponds in basins of the top surface, other local water surfaces where the relief allows, streams that run downhill, and waterfalls over an island's edge where a stream reaches the rim and the generation result allows it.
- Water bodies have their own deterministic placement parameters (in `WorldGenSettings`) and their own `RandomStream`.
- Stylized water surfaces: flat-shaded, translucent material, drawn after the opaque geometry with alpha blending; waterfalls as low-poly ribbons down the underside.
- No world ocean and no global water plane.

**Tests (headless):** every water body lies on or within its island (inside the footprint, at or below the surrounding terrain where it fills a basin); streams descend monotonically; waterfalls start at the rim; water meshes pass `geometry::validate()`; determinism; islands without suitable relief get no water. GPU smoke with water enabled.

**Done when:** a seed corpus produces valid local water on some islands and none elsewhere.

**Visual check:** screenshot with lakes, a stream and a waterfall.

### M3.10 — Clouds

**Goal:** minimalist low-poly clouds in the air between islands.

**Scope:**
- Deterministic cloud clusters of low-poly shapes around and between islands at different altitudes, with different sizes and densities, from their own `RandomStream::Clouds` seed.
- Clouds keep clearance from island volumes.
- Slow drift along a wind direction from the settings, computed from the initial position and time (a display effect, wrapped inside the world bounds); the initial placement repeats exactly for a seed.

**Tests (headless):** counts, sizes, densities and altitudes within the settings; inside the world bounds; clearance from islands; determinism of the initial placement; the drift formula (position at time t, wrapping). GPU smoke with clouds.

**Done when:** clouds appear at varied altitudes, repeat for a seed and drift without errors.

**Visual check:** screenshot with clouds between islands.

### M3.11 — Settings as versioned JSON

**Goal:** save and load a world description.

**Scope:**
- `nlohmann_json/3.12.0` as decided in ADR 0004: the envelope with `format` and `formatVersion`, `generatorVersion`, the seed as a hex string, all `WorldGenSettings` fields including placement, altitude and size ranges, local water and clouds.
- Loading validates with the M3.3 rules; errors never throw across the boundary and name the field path.
- island-preview can start from a settings file.

**Tests (headless):** the ADR 0004 list: round trip, missing fields (defaults), unknown fields (warning), wrong types, malformed JSON, a newer `formatVersion`, a seed above 2^53; a loaded file reproduces the same world hash.

**Done when:** save → load → generate gives the same world and every failure case is reported.

**Visual check:** none.

### M3.12 — WorldGen panel with Dear ImGui

**Goal:** change parameters and regenerate without restarting.

**Scope:**
- Dear ImGui as a development and debug UI in the sandbox, with its SDL3 platform and OpenGL 3 renderer backends; the full editor stays M5. An ADR records the package version and where the OpenGL-dependent backend lives, so no OpenGL type leaves the renderer boundary (AGENTS.md §3).
- Panel: seed, generator settings, island count, altitude ranges, size ranges, spacing, shape and noise parameters, local water and clouds, "Regenerate", save and load (M3.11). Values are clamped with the M3.3 rules.
- Regeneration replaces meshes and materials without leaking renderer resources.

**Tests:** headless tests for the settings-to-UI binding logic where it is separable; GPU smoke with the panel enabled; renderer resource counts stay level after repeated regeneration; manual check by the user.

**Done when:** every parameter can be edited and the world regenerates in place.

**Visual check:** screenshot with the panel.

### M3.13 — Final verification and pull request

**Goal:** close M3 against its exit criterion. No new features.

**Scope:**
- Full checks: Debug and Release with warnings as errors, headless and GPU tiers in both configurations, format check, a fresh clone, CI green on Windows and Linux.
- A seed corpus: different seeds give visibly and measurably different archipelagos; the same seed repeats (hashes); geometry, placement, water and clouds are valid for every seed.
- `docs/screenshots/m3/README.md` complete, with several seeds; M3 status in `docs/ENGINE_PLAN.md`; `README.md`; ADR statuses.
- Pull request `milestone/m3-floating-archipelago` → `main`, merged only after the user accepts M3.

## Risks and open questions

- Cross-platform float equality of generated geometry is not guaranteed (ADR 0003): repeatability is checked bitwise within one binary and with quantized hashes across platforms.
- Rim angles and underside profiles may need trigonometry; ADR 0003 allows it where needed, with tolerance-based checks.
- Translucent water and clouds are sorted only coarsely (opaque first, then translucent objects back to front); overlapping translucent surfaces may show sorting artifacts.
- Atmospheric haze or fog would need a shader change and is not planned in M3; depth is conveyed by palettes, the sky gradient and composition.
- Island counts and mesh resolution drive triangle counts; the settings limit them, and the overlay shows the totals.
