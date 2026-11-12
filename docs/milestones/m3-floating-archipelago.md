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
| M3.1 | Deterministic randomness | done |
| M3.2 | Noise | done |
| M3.3 | World generation settings | in progress |
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

**Scope** (decisions taken while implementing, ADR 0003 Amendment 1):
- New module `engine/worldgen` (library code only, no rendering): `engine/include/engine/worldgen/Random.hpp` and `engine/src/worldgen/Random.cpp`, namespace `engine::worldgen`, part of the `engine` static library like the other domains.
- `mix64` (SplitMix64 finalizer), `deriveSeed(masterSeed, stream, index, generatorVersion)`, `enum class RandomStream` with fixed values, `Pcg32` (XSH-RR, 64-bit state) as a value type, `nextFloat01()` and `nextBounded(n)` (Lemire with rejection), ranges and booleans built on them.
- `deriveSeed` folds its four inputs with `fold(h, x) = mix64((h ^ x) + 0x9E3779B97F4A7C15)`, starting from 0. Every fold is a bijection of either argument, so changing any single input always changes the seed; the first fold is SplitMix64's first output. `index` is 64-bit, `generatorVersion` 32-bit.
- `RandomStream` has exactly the five values of ADR 0003 (`IslandLayout = 1` … `Clouds = 5`); later streams get the next free value in their own step.
- `Pcg32(seed, sequence = kDefaultSequence)` seeds like the reference `pcg32_srandom_r`; the default sequence gives the increment of `pcg32` in pcg-cpp.
- Ranges and booleans: `nextFloat(min, max)` in [min, max), computed in double; `nextInt(min, max)` with both ends included, over the whole int32 range if asked; `nextBool(p = 0.5)`. `nextFloat01`, `nextFloat` and `nextBool` always consume exactly one output, also for empty or invalid ranges (which return `min`), so changing one range in the settings does not shift later draws. `nextBounded(0)` returns 0.
- `-ffp-contract=off` for worldgen sources on GCC/Clang (the `worldgen_sources` list in `engine/CMakeLists.txt`); no fast-math anywhere: `Random.cpp` stops the build if fast-math or floating-point contraction is enabled (`__FAST_MATH__`, `_M_FP_FAST`, `_M_FP_CONTRACT`).
- The generator version itself (a constant and a settings field) comes with the settings in M3.3; no world generation code uses the module yet.

**Tests (headless):** exact golden values for `mix64`, `deriveSeed`, the first outputs of `Pcg32` for fixed seeds and `nextBounded`, identical on Windows and Linux in CI; `nextFloat01` stays in [0, 1); `nextBounded` covers its range without bias on a large sample; different streams and indices give different seeds. Implemented in `tests/unit/worldgen/random_test.cpp`: SplitMix64 (seed 0) and PCG32 (seed 42, sequence 54) match the published outputs of their reference implementations; `deriveSeed`, the default sequence, `nextBounded` (including the outputs its rejections consume), `nextFloat` (bit patterns), `nextInt` and `nextBool` match values from an independent implementation; mix64 avalanche; 15000 stream/index/version combinations per master seed give distinct seeds; copies and sequences of `Pcg32`; a chi-square test of `nextBounded(6)` and a test at bound 3·2^30 that a modulo reduction would fail; range ends, empty and invalid ranges, and the number of outputs each call consumes.

**Done when:** the golden tests pass in CI on all four jobs.

**Visual check:** none.

### M3.2 — Noise

**Goal:** deterministic noise for shapes and relief.

**Scope** (decisions taken while implementing, ADR 0003 Amendment 2):
- 2D gradient noise with a polynomial fade curve, seeded through M3.1 (no global permutation table).
- fBm (octaves, persistence, lacunarity; amplitudes by repeated multiplication instead of `std::pow`), ridge noise, domain warp.
- Pure functions of position, seed and parameters.
- `engine/include/engine/worldgen/Noise.hpp` and `engine/src/worldgen/Noise.cpp`: `gradientNoise(seed, position)` in [-1, 1], `fbm` in [-1, 1], `ridgeNoise` in [0, 1] (each octave folded into (1 − |n|)²), `domainWarp(seed, position, strength, params)` returning the moved position; `FractalNoiseParams` holds octaves, lacunarity and persistence. Positions are `math::Vec2` in noise cells, with no frequency parameter: callers scale world positions first.
- Each lattice point's gradient is one of 16 unit vectors written as literals, chosen by `mix64(key + packed coordinates)`; the key is `mix64(seed)`, or for octave k the k-th output of the seed's SplitMix64 sequence. Every octave also gets an offset below one cell from its key, so octave lattices do not line up. The scale factor sqrt(2) maps the largest possible value sqrt(1/2) to 1 (checked numerically before choosing it).
- Arithmetic in double from the float inputs, one rounding to float at the end; coordinates whose magnitude reaches 2^52, and non-finite ones, sample as 0.
- Parameters are clamped, not rejected: octaves [1, 12], lacunarity [1, 4], persistence [0, 1], warp strength [0, 64], NaN to the lower end (constants in `Noise.hpp`). Validation and clamping of the world settings stay in M3.3.
- The M3.1 fast-math guard moved into the internal header `engine/src/worldgen/FloatingPointRules.hpp`, included by every worldgen source; `kSplitMix64Gamma` became public in `Random.hpp`. M3.1's results are unchanged (its golden tests pass).

**Tests (headless):** output ranges for every function; continuity (small steps give small changes); bitwise repeatability in the same binary; stored samples compared with a tolerance across platforms; parameter edge cases (one octave, zero warp). Implemented in `tests/unit/worldgen/noise_test.cpp`: golden samples of all four functions (several seeds and parameter sets) against an independent implementation, tolerance 1e-6; ranges over 100000 positions, including extreme parameters, and that the range is actually used; gradient noise is 0 at lattice points; changes over steps of 1e-3 cells stay below a slope of 11; two evaluations give identical bits; different seeds give clearly different values; fbm is not 0 at integer positions (octave offsets); out-of-range parameters behave like their clamped values, one octave ignores lacunarity and persistence, persistence 0 equals one octave; warp strength 0, negative or NaN returns the position bit for bit, and each coordinate moves by at most the strength; samples outside the domain are 0, and the lattice repeats every 2^32 cells.

**Done when:** all noise functions are covered by range, repeatability and golden-sample tests.

**Visual check:** none.

### M3.3 — World generation settings

**Goal:** one versioned description of a world.

**Scope** (decisions taken while implementing, ADR 0013):
- `WorldGenSettings`: seed, generator version, island count, world bounds X/Y/Z, vertical altitude range, island size range, minimum horizontal and vertical spacing, shape parameters, elevation parameters, noise parameters, local water settings, cloud settings, mesh resolution.
- Documented defaults and a validation and clamping policy per field (reject or clamp, as `docs/ENGINE_PLAN.md` §6 asks), shared later by the JSON loader (M3.11) and the UI (M3.12).
- No world sea level: the concept is removed.
- `engine/include/engine/worldgen/WorldGenSettings.hpp` and `engine/src/worldgen/WorldGenSettings.cpp`. Groups follow the JSON of ADR 0004: `bounds`, `islands` (count, rim altitude, radius, spacings), `shape`, `elevation`, `underside`, `noise` (frequency, `FractalNoiseParams`, warp), `water`, `clouds`, `mesh`. 44 numeric fields plus seed, generator version and the water switch; the table with defaults and ranges is in ADR 0013.
- Defaults are member initializers; inclusive ranges are public constants in `engine::worldgen::limits`. `kGeneratorVersion` is 1.
- Policy: `validate()` rejects a wrong generator version, any numeric field outside its range (NaN and infinities included) and inconsistent combinations, returning the first problem in a fixed order with a message that starts with the field path; `clampToLimits()` moves single fields into their range (NaN to the default) and never repairs combinations.
- Combinations: each bounds axis spans at least 20 m; the bounds' diagonal is at most 1000 m (camera far plane); every range has min <= max; the largest island fits the bounds horizontally and, at the highest and lowest altitude, vertically (top `maxHeightRatio · radius`, underside `depthRatio.max · radius`); spacings are not larger than the world; cloud altitudes lie inside the bounds.
- Units: metres, wind direction in degrees (0 = +X, 90 = +Z), heights and depths as fractions of the island radius. An island is anchored at the centre of its rim.
- `FractalNoiseParams` gained a defaulted `operator==` so the settings compare as a whole.

**Tests (headless):** defaults are valid; every field below, at and above its limits; inconsistent combinations (altitude range outside the Y bounds, size range that cannot fit the bounds, spacing larger than the world) are rejected with a message naming the field. Implemented in `tests/unit/worldgen/world_gen_settings_test.cpp`, with the ranges written out from ADR 0013: for every float field the next float below and above each limit, NaN and both infinities are range problems naming the field, both limits pass the range check (and validate completely where no other field depends on them); every integer field likewise with limit ± 1 and the int extremes; exact messages for counts, small floats and NaN; the generator version; any seed; the fixed order of problems; 18 combinations rejected with their field path, among them altitude ranges above and below the Y bounds, a size range that cannot fit, spacings larger than the world, and a diagonal beyond the far plane; combinations exactly at their limits pass; `clampToLimits` moves every field to its limit or default and changes nothing else, keeps valid settings, and leaves combinations, seed, version and the water switch alone.

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
