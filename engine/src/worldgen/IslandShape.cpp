#include "engine/worldgen/IslandShape.hpp"

#include "FloatingPointRules.hpp"
#include "SubKeys.hpp"

#include "engine/worldgen/Random.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::worldgen {

namespace {

// Constants of the shape (ADR 0014). Changing any of them changes worlds.
constexpr float kMaxOutlineVariation = 0.4F; // times shape.outlineRoughness
constexpr double kOutlineCircle = 0.8;   // radius in noise cells of the circle read for the outline
constexpr double kMaskFalloff = 0.08;    // outer share of the outline where the mask falls to 0
constexpr double kCoreWeight = 0.3;      // central mass in the top
constexpr double kReliefWeight = 0.7;    // noise relief in the top
constexpr double kPlateauLevel = 0.6;    // top value above which a plateau is flat
constexpr double kTerraceFlat = 0.7;     // flat share of each terrace step
constexpr double kUndersideTaper = 0.35; // narrowing of the underside profile towards the tip
constexpr double kLedgeJitter = 0.8;     // times underside.roughness
constexpr double kLedgeFlat = 0.6;       // flat share of each ledge step
constexpr double kSpurScale = 2.5;       // noise cells per island radius
constexpr double kSpurDepth = 0.5;       // times underside.protrusion
constexpr double kDetailScale = 5.0;     // noise cells per island radius
constexpr double kDetailDepth = 0.15;    // times underside.roughness
constexpr int kDirectionAttempts = 16;

// Fixed parameters of the outline, ledge, spur and detail noise.
FractalNoiseParams shapeNoiseParams()
{
    FractalNoiseParams params;
    params.octaves = 3;
    params.lacunarity = 2.0F;
    params.persistence = 0.5F;
    return params;
}

using detail::subKey;

double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Staircase of value in [0, 1] with `count` steps: each step is flat for its
// first `flat` share and rises smoothly in the rest; 0 and 1 stay in place.
double steps(double value, int count, double flat)
{
    const double scaled = value * static_cast<double>(count);
    const double level = std::floor(scaled);
    const double rise = std::clamp((scaled - level - flat) / (1.0 - flat), 0.0, 1.0);
    return (level + fade(rise)) / static_cast<double>(count);
}

// Distance from the anchor and unit direction of a local position. The
// direction of the anchor itself is +X; a non-finite position is infinitely far.
struct Polar {
    double distance;
    double x;
    double z;
};

Polar polarOf(math::Vec2 local)
{
    const auto x = static_cast<double>(local.x);
    const auto z = static_cast<double>(local.y);
    const double distance = std::sqrt(x * x + z * z);
    if (!std::isfinite(distance)) {
        return {std::numeric_limits<double>::infinity(), 1.0, 0.0};
    }
    if (distance == 0.0) {
        return {0.0, 1.0, 0.0};
    }
    return {distance, x / distance, z / distance};
}

// The point on the circle read for direction-dependent noise.
math::Vec2 circlePoint(const Polar& polar)
{
    return math::Vec2{static_cast<float>(polar.x * kOutlineCircle),
                      static_cast<float>(polar.z * kOutlineCircle)};
}

double outlineRadiusOf(const IslandShape& shape, const Polar& polar)
{
    const auto axisX = static_cast<double>(shape.longAxis.x);
    const auto axisZ = static_cast<double>(shape.longAxis.y);
    const double along =
        (polar.x * axisX + polar.z * axisZ) / static_cast<double>(shape.longRadius);
    const double across =
        (polar.z * axisX - polar.x * axisZ) / static_cast<double>(shape.shortRadius);
    const double ellipse = 1.0 / std::sqrt(along * along + across * across);

    const FractalNoiseParams params = shapeNoiseParams();
    const math::Vec2 warped =
        domainWarp(shape.outlineWarpKey, circlePoint(polar), shape.outlineWarp, params);
    const auto noise = static_cast<double>(fbm(shape.outlineKey, warped, params));
    const double radius = ellipse * (1.0 + static_cast<double>(shape.outlineVariation) * noise);
    // Rounding must not push the outline past the bounding circle.
    return std::min(radius, static_cast<double>(shape.island.radius));
}

double distanceOf(const IslandShape& shape, const Polar& polar)
{
    if (polar.distance == 0.0) {
        return 0.0;
    }
    return polar.distance / outlineRadiusOf(shape, polar);
}

double maskOf(double s)
{
    if (s <= 1.0 - kMaskFalloff) {
        return 1.0;
    }
    if (!(s < 1.0)) {
        return 0.0;
    }
    return fade((1.0 - s) / kMaskFalloff);
}

} // namespace

IslandShape makeIslandShape(const PlacedIsland& island, const WorldGenSettings& settings)
{
    IslandShape shape;
    shape.island = island;

    // Shape seed: scalars first (fixed order), then the noise keys.
    Pcg32 scalars{subKey(island.shapeSeed, 0U)};
    const float ellipticity = scalars.nextFloat(0.0F, settings.shape.ellipticity);
    // A uniform direction without trigonometry: points of the square, kept if
    // they lie in the ring between radius 1/4 and 1. The direction is drawn
    // last, because it takes a varying number of outputs.
    for (int attempt = 0; attempt < kDirectionAttempts; ++attempt) {
        const float x = scalars.nextFloat(-1.0F, 1.0F);
        const float z = scalars.nextFloat(-1.0F, 1.0F);
        const double lengthSquared = static_cast<double>(x) * static_cast<double>(x) +
                                     static_cast<double>(z) * static_cast<double>(z);
        if (lengthSquared >= 0.0625 && lengthSquared <= 1.0) {
            const double length = std::sqrt(lengthSquared);
            shape.longAxis = math::Vec2{static_cast<float>(static_cast<double>(x) / length),
                                        static_cast<float>(static_cast<double>(z) / length)};
            break;
        }
    }
    shape.outlineVariation = kMaxOutlineVariation * settings.shape.outlineRoughness;
    shape.longRadius = island.radius / (1.0F + shape.outlineVariation);
    shape.shortRadius = shape.longRadius * (1.0F - ellipticity);
    shape.outlineWarp = settings.shape.outlineWarp;
    shape.outlineKey = subKey(island.shapeSeed, 1U);
    shape.outlineWarpKey = subKey(island.shapeSeed, 2U);
    shape.ledgeKey = subKey(island.shapeSeed, 3U);
    shape.spurKey = subKey(island.shapeSeed, 4U);
    shape.detailKey = subKey(island.shapeSeed, 5U);

    // Terrain seed: its own stream, so the relief can change without moving
    // the outlines.
    const std::uint64_t terrainSeed =
        deriveSeed(settings.seed, RandomStream::Terrain, island.index, settings.generatorVersion);
    Pcg32 terrainScalars{subKey(terrainSeed, 0U)};
    shape.terraced = terrainScalars.nextBool(settings.elevation.plateauChance);
    shape.reliefWarpKey = subKey(terrainSeed, 1U);
    shape.reliefKey = subKey(terrainSeed, 2U);
    shape.ridgeKey = subKey(terrainSeed, 3U);

    shape.relief = settings.noise;
    shape.ridgeWeight = settings.elevation.ridgeWeight;
    shape.terraceSteps = settings.elevation.terraceSteps;
    shape.ledgeCount = settings.underside.ledgeCount;
    shape.protrusion = settings.underside.protrusion;
    shape.roughness = settings.underside.roughness;
    return shape;
}

float outlineRadius(const IslandShape& shape, math::Vec2 direction)
{
    Polar polar = polarOf(direction);
    if (!std::isfinite(polar.distance)) {
        polar = Polar{1.0, 1.0, 0.0};
    }
    return static_cast<float>(outlineRadiusOf(shape, polar));
}

float outlineDistance(const IslandShape& shape, math::Vec2 local)
{
    return static_cast<float>(distanceOf(shape, polarOf(local)));
}

float islandMask(const IslandShape& shape, math::Vec2 local)
{
    return static_cast<float>(maskOf(distanceOf(shape, polarOf(local))));
}

float topHeightAt(const IslandShape& shape, math::Vec2 local)
{
    const double s = distanceOf(shape, polarOf(local));
    if (!(s < 1.0)) {
        return 0.0F;
    }
    const NoiseSettings& relief = shape.relief;
    const math::Vec2 noisePosition{local.x * relief.frequency, local.y * relief.frequency};
    const math::Vec2 warped =
        domainWarp(shape.reliefWarpKey, noisePosition, relief.warpStrength, relief.fractal);
    const double hills = std::clamp(
        0.5 + 0.75 * static_cast<double>(fbm(shape.reliefKey, warped, relief.fractal)), 0.0, 1.0);
    const auto ridges = static_cast<double>(ridgeNoise(shape.ridgeKey, warped, relief.fractal));
    const auto ridgeWeight = static_cast<double>(shape.ridgeWeight);
    const double noise = (1.0 - ridgeWeight) * hills + ridgeWeight * ridges;

    double elevation = maskOf(s) * (kCoreWeight * (1.0 - s) + kReliefWeight * noise);
    if (shape.terraced) {
        elevation = shape.terraceSteps == 0 ? std::min(elevation / kPlateauLevel, 1.0)
                                            : steps(elevation, shape.terraceSteps, kTerraceFlat);
    }
    return static_cast<float>(static_cast<double>(shape.island.topHeight) * elevation);
}

float undersideDepthAt(const IslandShape& shape, math::Vec2 local)
{
    const Polar polar = polarOf(local);
    const double s = distanceOf(shape, polar);
    if (!(s < 1.0)) {
        return 0.0F;
    }
    const FractalNoiseParams params = shapeNoiseParams();
    const auto roughness = static_cast<double>(shape.roughness);

    // Profile: vertical at the rim, narrowing to a point at the anchor.
    const double profile = std::sqrt(1.0 - s) * (1.0 - kUndersideTaper * s);
    // Ledges at depths that vary around the island; 0 and 1 stay in place.
    const auto jitter = static_cast<double>(fbm(shape.ledgeKey, circlePoint(polar), params));
    double depth = std::clamp(
        profile + kLedgeJitter * roughness * jitter * profile * (1.0 - profile), 0.0, 1.0);
    if (shape.ledgeCount > 0) {
        depth = steps(depth, shape.ledgeCount + 1, kLedgeFlat);
    }

    // Spurs and rock detail between rim and tip.
    const double envelope = 4.0 * s * (1.0 - s);
    const auto radius = static_cast<double>(shape.island.radius);
    const auto spurScale = static_cast<float>(kSpurScale / radius);
    const auto spur = static_cast<double>(
        ridgeNoise(shape.spurKey, math::Vec2{local.x * spurScale, local.y * spurScale}, params));
    depth += kSpurDepth * static_cast<double>(shape.protrusion) * spur * spur * spur * envelope;
    const auto detailScale = static_cast<float>(kDetailScale / radius);
    const auto detail = static_cast<double>(
        fbm(shape.detailKey, math::Vec2{local.x * detailScale, local.y * detailScale}, params));
    depth += kDetailDepth * roughness * detail * envelope;

    depth = std::clamp(depth, 0.0, 1.0);
    return static_cast<float>(static_cast<double>(shape.island.undersideDepth) * depth);
}

} // namespace engine::worldgen
