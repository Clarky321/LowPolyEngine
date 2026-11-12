#pragma once

// Deterministic 2D noise for island shapes and relief (ADR 0003, Amendment 2).
//
// Every function is pure: its result depends only on the seed, the position and
// the parameters, never on global state such as a shared permutation table.
// Positions are in noise cells: the lattice of gradientNoise and of the first
// octave of the fractal functions has a spacing of 1, so callers multiply world
// positions by a frequency first. The float inputs are evaluated in double with
// correctly rounded IEEE operations only (no transcendental functions, no
// fast-math, no contraction), so results repeat bit for bit in one binary and
// on every platform. The formulas are part of the save-compatibility contract.
//
// Coordinates whose magnitude reaches 2^52 cells (after scaling for an octave),
// and non-finite ones, are outside the noise domain: such a sample is 0.

#include "engine/core/Math.hpp"

#include <cstdint>

namespace engine::worldgen {

inline constexpr int kMaxNoiseOctaves = 12;
inline constexpr float kMaxNoiseLacunarity = 4.0F;
inline constexpr float kMaxWarpStrength = 64.0F;

// Parameters of the fractal functions (fbm, ridgeNoise, domainWarp). Values
// outside their range are clamped to it; NaN counts as the lower end.
struct FractalNoiseParams {
    // Number of layers, [1, kMaxNoiseOctaves].
    int octaves = 4;
    // Frequency factor from one octave to the next, [1, kMaxNoiseLacunarity].
    float lacunarity = 2.0F;
    // Amplitude factor from one octave to the next, [0, 1].
    float persistence = 0.5F;
};

// Gradient (Perlin) noise in [-1, 1]: 0 at every lattice point, smooth in
// between (quintic fade curve). Each lattice point takes one of 16 unit
// gradients from mix64(mix64(seed) + its packed coordinates), so a seed selects
// a different region of one unbounded pattern.
[[nodiscard]] float gradientNoise(std::uint64_t seed, math::Vec2 position);

// Fractal Brownian motion in [-1, 1]: the sum of params.octaves layers of
// gradient noise. Octave k (from 0) is sampled at position * lacunarity^k plus
// an offset below one cell, with amplitude persistence^k (both by repeated
// multiplication); the sum is divided by the sum of the amplitudes. Each octave
// has its own key and offset from the SplitMix64 sequence of the seed, so the
// lattice points of the octaves do not line up.
[[nodiscard]] float fbm(std::uint64_t seed, math::Vec2 position, const FractalNoiseParams& params);

// Ridge noise in [0, 1]: like fbm, but every octave value n is folded into
// (1 - |n|)^2 before it is summed, which turns the zero lines of the noise into
// sharp crests. Same keys and offsets as fbm for the same seed.
[[nodiscard]] float ridgeNoise(std::uint64_t seed, math::Vec2 position,
                               const FractalNoiseParams& params);

// Domain warp: position moved by strength times two fbm values, one per axis,
// whose seeds are the first two outputs of the SplitMix64 sequence of the
// seed. Each coordinate moves by at most strength cells. strength is clamped
// to [0, kMaxWarpStrength] (NaN counts as 0); at 0 the position is returned
// unchanged. Sample another noise function at the result to warp it.
[[nodiscard]] math::Vec2 domainWarp(std::uint64_t seed, math::Vec2 position, float strength,
                                    const FractalNoiseParams& params);

} // namespace engine::worldgen
