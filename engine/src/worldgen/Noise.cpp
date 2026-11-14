#include "engine/worldgen/Noise.hpp"

#include "FloatingPointRules.hpp"

#include "engine/worldgen/Random.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace engine::worldgen {

namespace {

struct Gradient {
    double x;
    double y;
};

// 16 unit vectors at multiples of 22.5 degrees, written out so no trigonometric
// function is evaluated: cos 22.5, sin 22.5 and sqrt(1/2) as correctly rounded
// double literals.
constexpr double kCos22 = 0.9238795325112867;
constexpr double kSin22 = 0.3826834323650898;
constexpr double kHalfSqrt2 = 0.7071067811865476;
constexpr std::array<Gradient, 16> kGradients{{
    {1.0, 0.0},
    {kCos22, kSin22},
    {kHalfSqrt2, kHalfSqrt2},
    {kSin22, kCos22},
    {0.0, 1.0},
    {-kSin22, kCos22},
    {-kHalfSqrt2, kHalfSqrt2},
    {-kCos22, kSin22},
    {-1.0, 0.0},
    {-kCos22, -kSin22},
    {-kHalfSqrt2, -kHalfSqrt2},
    {-kSin22, -kCos22},
    {0.0, -1.0},
    {kSin22, -kCos22},
    {kHalfSqrt2, -kHalfSqrt2},
    {kCos22, -kSin22},
}};

// With unit gradients, 2D gradient noise reaches at most sqrt(1/2), at the
// centre of a cell whose four gradients all point at it; sqrt(2) maps that
// to 1.
constexpr double kNoiseScale = 1.4142135623730951;

// Floor and fraction are exact below 2^52, and the cell index fits an int64.
constexpr double kDomainLimit = 0x1p52;

// The per-octave offset uses 24 bits of the octave key per axis.
constexpr std::uint64_t kOffsetMask = 0xFFFFFFU;
constexpr double kOffsetScale = 0x1p-24;

double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Dot product of a lattice point's gradient with the offset (dx, dy) from it.
// The point's two 32-bit coordinates are packed into one 64-bit value; adding
// the key translates the lattice, so a key selects a region of one pattern.
double corner(std::uint64_t key, std::int64_t ix, std::int64_t iy, double dx, double dy)
{
    const std::uint64_t packed =
        (static_cast<std::uint64_t>(iy) << 32U) | (static_cast<std::uint64_t>(ix) & 0xFFFFFFFFU);
    const Gradient& gradient = kGradients[mix64(key + packed) >> 60U];
    return gradient.x * dx + gradient.y * dy;
}

// Gradient noise of one lattice, in [-1, 1].
double latticeNoise(std::uint64_t key, double x, double y)
{
    // Also false for NaN.
    if (!(std::abs(x) < kDomainLimit && std::abs(y) < kDomainLimit)) {
        return 0.0;
    }
    const double cellX = std::floor(x);
    const double cellY = std::floor(y);
    const double fx = x - cellX;
    const double fy = y - cellY;
    const auto ix = static_cast<std::int64_t>(cellX);
    const auto iy = static_cast<std::int64_t>(cellY);

    const double n00 = corner(key, ix, iy, fx, fy);
    const double n10 = corner(key, ix + 1, iy, fx - 1.0, fy);
    const double n01 = corner(key, ix, iy + 1, fx, fy - 1.0);
    const double n11 = corner(key, ix + 1, iy + 1, fx - 1.0, fy - 1.0);
    const double u = fade(fx);
    const double v = fade(fy);
    const double bottom = n00 + u * (n10 - n00);
    const double top = n01 + u * (n11 - n01);
    const double value = (bottom + v * (top - bottom)) * kNoiseScale;
    return std::clamp(value, -1.0, 1.0);
}

// value clamped to [low, high]; NaN becomes low.
float clampParameter(float value, float low, float high)
{
    if (!(value >= low)) {
        return low;
    }
    return value > high ? high : value;
}

enum class Fold { None, Ridge };

// Sum of the octaves, divided by the sum of their amplitudes.
double fractal(std::uint64_t seed, double x, double y, const FractalNoiseParams& params, Fold fold)
{
    const int octaves = std::clamp(params.octaves, 1, kMaxNoiseOctaves);
    const auto lacunarity =
        static_cast<double>(clampParameter(params.lacunarity, 1.0F, kMaxNoiseLacunarity));
    const auto persistence = static_cast<double>(clampParameter(params.persistence, 0.0F, 1.0F));

    double sum = 0.0;
    double amplitudes = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    std::uint64_t state = seed;
    for (int octave = 0; octave < octaves; ++octave) {
        state += kSplitMix64Gamma;
        const std::uint64_t key = mix64(state);
        const double offsetX = static_cast<double>(key & kOffsetMask) * kOffsetScale;
        const double offsetY = static_cast<double>((key >> 24U) & kOffsetMask) * kOffsetScale;
        double value = latticeNoise(key, x * frequency + offsetX, y * frequency + offsetY);
        if (fold == Fold::Ridge) {
            const double crest = 1.0 - std::abs(value);
            value = crest * crest;
        }
        sum += amplitude * value;
        amplitudes += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    // amplitudes >= 1: the first octave has amplitude 1.
    const double low = fold == Fold::Ridge ? 0.0 : -1.0;
    return std::clamp(sum / amplitudes, low, 1.0);
}

} // namespace

float gradientNoise(std::uint64_t seed, math::Vec2 position)
{
    return static_cast<float>(latticeNoise(mix64(seed), static_cast<double>(position.x),
                                           static_cast<double>(position.y)));
}

float fbm(std::uint64_t seed, math::Vec2 position, const FractalNoiseParams& params)
{
    return static_cast<float>(fractal(seed, static_cast<double>(position.x),
                                      static_cast<double>(position.y), params, Fold::None));
}

float ridgeNoise(std::uint64_t seed, math::Vec2 position, const FractalNoiseParams& params)
{
    return static_cast<float>(fractal(seed, static_cast<double>(position.x),
                                      static_cast<double>(position.y), params, Fold::Ridge));
}

math::Vec2 domainWarp(std::uint64_t seed, math::Vec2 position, float strength,
                      const FractalNoiseParams& params)
{
    const float clampedStrength = clampParameter(strength, 0.0F, kMaxWarpStrength);
    if (clampedStrength == 0.0F) {
        return position;
    }
    const auto x = static_cast<double>(position.x);
    const auto y = static_cast<double>(position.y);
    const auto amount = static_cast<double>(clampedStrength);
    // The first two outputs of the seed's SplitMix64 sequence seed the two axes.
    std::uint64_t state = seed + kSplitMix64Gamma;
    const double offsetX = fractal(mix64(state), x, y, params, Fold::None);
    state += kSplitMix64Gamma;
    const double offsetY = fractal(mix64(state), x, y, params, Fold::None);
    return math::Vec2{static_cast<float>(x + amount * offsetX),
                      static_cast<float>(y + amount * offsetY)};
}

} // namespace engine::worldgen
