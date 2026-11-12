# ADR 0003: Deterministic procedural generation

- **Status:** Accepted; amended for M3.1 (Amendment 1)
- **Date:** 2026-11-09
- **Related:** `docs/ENGINE_PLAN.md` §6, `docs/TESTING_AND_CI.md` §2, AGENTS.md §3

## Context

Islands, vegetation and clouds are generated from a seed. The same seed, settings and generator version must give the same world: for saved projects, bug reports, tests and future multiplayer/streaming. The engine is built with MSVC on Windows and GCC/Clang on Linux.

## Problem

The C++ standard library does not give reproducible random numbers across implementations:

- `std::uniform_int_distribution`, `std::uniform_real_distribution` and other distributions are implementation-defined; MSVC STL and libstdc++ return different values for the same engine state.
- `std::hash` values are implementation-defined and may change between library versions.
- `std::random_device`, time and frame timing are non-deterministic.
- Iteration order of `std::unordered_map`/`unordered_set` is unspecified.

Floating-point results can also differ between compilers: fused multiply-add contraction, fast-math flags and different implementations of `std::sin`, `std::pow`, `std::exp`.

## Options

1. `std::mt19937` + standard distributions — engines are specified, distributions are not. Rejected.
2. Project-owned PRNG, hash and distributions — small amount of code, fully specified, testable with fixed expected values.
3. Third-party random library (e.g. PCG reference implementation) — adds a dependency for ~50 lines of code. Rejected for now.

## Decision

Use project-owned code in `engine/worldgen` (no OpenGL, SDL or ImGui dependency):

1. **Seed mixing/hash: SplitMix64.** `mix64(x)` is the SplitMix64 finalizer. Seed derivation:
   `deriveSeed(masterSeed, stream, index, generatorVersion)` folds each value into the state with `mix64`. `stream` is an `enum class RandomStream : std::uint64_t { IslandLayout = 1, IslandShape = 2, Terrain = 3, Vegetation = 4, Clouds = 5, ... }`; values are fixed forever and never reused. New features get new stream values, so adding a feature does not change existing random sequences.
2. **Random sequence: PCG32 (XSH-RR, 64-bit state).** One `Pcg32` instance per derived seed. It is a value type: no globals, no thread-local state.
3. **Distributions are project code:**
   - `nextFloat01()` = `(next() >> 8) * 0x1.0p-24f` — exact, in `[0, 1)`.
   - `nextBounded(n)` = Lemire's multiply-shift with rejection — unbiased and integer-only.
   - Ranges/booleans are built on these two.
4. **No `std::hash`, `std::random_device`, clocks or unordered-container iteration order** in generation code.
5. **Floating-point rules for generation code:**
   - Do not enable fast-math (`/fp:fast`, `-ffast-math`) for the engine.
   - Disable FMA contraction for worldgen sources on GCC/Clang (`-ffp-contract=off`); MSVC `/fp:precise` (default) does not contract across statements.
   - Avoid transcendental functions in noise and height evaluation where a polynomial works (fade curves, octave amplitude by repeated multiplication instead of `std::pow`). Where they are needed, results are only compared with tolerances.
6. **`generatorVersion`** (unsigned integer) is part of the generator settings and the seed derivation. Any change that alters generated output for the same inputs increments it.

### Test strategy

- **Exact golden values** for integer outputs: `mix64`, `deriveSeed`, the first N outputs of `Pcg32` for fixed seeds, `nextBounded`. These must match on every platform in CI.
- **Tolerance checks** for float outputs (noise samples, heights) against stored expected values.
- **Same-binary repeatability**: generating twice from the same inputs gives identical vertex/index data (bitwise), checked with a stable hash of the mesh data.
- **Cross-platform mesh equality** is a goal, checked by comparing a hash of positions quantized to a documented grid (e.g. 1/1024 world units). If CI shows a mismatch, it is investigated, not silenced.
- Invariants from `docs/TESTING_AND_CI.md` §2 (finite values, valid indices, bounds) on every generated mesh.

## Consequences

- WorldGen is a pure CPU module producing `MeshData`/`WorldData`. The renderer consumes this data; WorldGen does not depend on the renderer.
- About 100–150 lines of PRNG/hash code to own and test; no new dependency.
- `RandomStream` values and the `deriveSeed` formula are part of the save-compatibility contract: changing them requires a `generatorVersion` bump.
- Exact cross-compiler equality of float geometry is not guaranteed; the contract is "same logical result", verified with tolerances and quantized hashes.

## Amendment 1: exact formulas (M3.1, 2026-11-12)

M3.1 implements the decision in `engine/worldgen/Random.hpp` (namespace `engine::worldgen`). The formulas below, the `RandomStream` values and the number of generator outputs each call consumes are part of the save-compatibility contract: changing any of them requires a `generatorVersion` bump.

- **`mix64`** is the SplitMix64 finalizer: `z = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9`, `z = (z ^ (z >> 27)) * 0x94D049BB133111EB`, result `z ^ (z >> 31)`.
- **`deriveSeed`.** With `fold(h, x) = mix64((h ^ x) + 0x9E3779B97F4A7C15)` (SplitMix64's gamma), `deriveSeed(masterSeed, stream, index, generatorVersion) = fold(fold(fold(fold(0, masterSeed), stream), index), generatorVersion)`. `index` is a `std::uint64_t`, `generatorVersion` a `std::uint32_t`. Each fold is a bijection of either argument while the other is fixed, so changing any single input always changes the seed. `fold(0, m)` equals the first output of SplitMix64 seeded with `m`, which ties the code to SplitMix64's published reference outputs.
- **`RandomStream`** has exactly the values listed in the decision (`IslandLayout = 1`, `IslandShape = 2`, `Terrain = 3`, `Vegetation = 4`, `Clouds = 5`). A further stream (local water, M3.9) gets the next free value in the step that introduces it.
- **`Pcg32`** is seeded as in the reference `pcg32_srandom_r(seed, sequence)`: increment `(sequence << 1) | 1`, one step, add the seed, one step. The outputs of the reference demo (seed 42, sequence 54) are therefore golden values. The default sequence `0x0A02BDBF7BB3C0A7` gives the increment 1442695040888963407, the default of `pcg32` in the reference C++ library. Generators compare with `==`; copies continue the same sequence.
- **Distributions and consumed outputs.** A draw consumes a fixed number of outputs wherever possible, so changing one range in the settings does not shift the later draws of the same stream:
  - `nextFloat01()` = `(next() >> 8) * 2^-24`, exact, in [0, 1); one output.
  - `nextBounded(n)`: Lemire's multiply-shift, rejecting while the low 32 bits of `next() * n` are below `2^32 mod n` (computed only when the low word is below `n`). One output plus one per rejection. `n = 0` is an empty range: result 0, one output.
  - `nextFloat(min, max)` = `float(double(min) + (double(max) - double(min)) * double(nextFloat01()))`, replaced by the largest float below `max` when rounding reaches `max`; the result is in [min, max). Exactly one output, also for an empty or invalid range (`min >= max`, or a non-finite end), which returns `min` unchanged. The double arithmetic cannot overflow for finite floats.
  - `nextInt(min, max)` includes both ends: `min + nextBounded(max - min + 1)`, or `min + next()` for the whole int32 range. `max < min` counts as `max == min`.
  - `nextBool(p)` = `nextFloat01() < p`, one output: always false for `p <= 0` or NaN, always true for `p >= 1`.
- **Floating-point build rules.** Worldgen sources are listed in `worldgen_sources` in `engine/CMakeLists.txt`, which gives them `-ffp-contract=off` on GCC and Clang. `engine/src/worldgen/Random.cpp` stops the build with `#error` if `__FAST_MATH__`, `_M_FP_FAST` or `_M_FP_CONTRACT` is defined. The float results above come from a few correctly rounded IEEE operations on x64 (SSE2; there is no x87 target), so they are tested bit for bit on every platform.
- **Module.** WorldGen is part of the `engine` static library (`engine/src/worldgen/`), like the other domains (`docs/PROJECT_STRUCTURE.md`); its sources include no renderer, SDL or ImGui headers.

`tests/unit/worldgen/random_test.cpp` checks SplitMix64 and PCG32 against their reference outputs and every other formula against golden values from an independent implementation, plus ranges, bias and consumed outputs.
