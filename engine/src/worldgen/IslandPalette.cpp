#include "engine/worldgen/IslandPalette.hpp"

#include "FloatingPointRules.hpp"

#include "engine/worldgen/Random.hpp"

#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>

namespace engine::worldgen {

namespace {

using math::Vec3;

// Built-in palettes, display-space RGB (ADR 0015). Each names its look.
constexpr std::array<IslandPalette, 6> kPalettes{{
    // Meadow: spring green, warm earth.
    {Vec3{0.53F, 0.74F, 0.38F}, Vec3{0.58F, 0.45F, 0.32F}, Vec3{0.91F, 0.84F, 0.64F},
     Vec3{0.60F, 0.58F, 0.55F}, Vec3{0.96F, 0.97F, 0.99F}, Vec3{0.56F, 0.50F, 0.46F},
     Vec3{0.32F, 0.27F, 0.30F}},
    // Autumn: orange grass, red-brown rock.
    {Vec3{0.89F, 0.60F, 0.30F}, Vec3{0.60F, 0.38F, 0.26F}, Vec3{0.94F, 0.82F, 0.60F},
     Vec3{0.64F, 0.53F, 0.47F}, Vec3{0.98F, 0.94F, 0.88F}, Vec3{0.62F, 0.46F, 0.40F},
     Vec3{0.34F, 0.22F, 0.24F}},
    // Teal moss: blue-green grass, slate rock.
    {Vec3{0.38F, 0.72F, 0.64F}, Vec3{0.40F, 0.43F, 0.36F}, Vec3{0.86F, 0.88F, 0.74F},
     Vec3{0.50F, 0.57F, 0.60F}, Vec3{0.92F, 0.98F, 0.99F}, Vec3{0.42F, 0.49F, 0.54F},
     Vec3{0.21F, 0.25F, 0.34F}},
    // Sandstone: dry grass, rose rock.
    {Vec3{0.86F, 0.70F, 0.48F}, Vec3{0.78F, 0.49F, 0.38F}, Vec3{0.96F, 0.86F, 0.68F},
     Vec3{0.84F, 0.56F, 0.45F}, Vec3{0.99F, 0.93F, 0.87F}, Vec3{0.74F, 0.48F, 0.42F},
     Vec3{0.44F, 0.26F, 0.28F}},
    // Lavender dusk: violet grass, cool stone.
    {Vec3{0.64F, 0.58F, 0.84F}, Vec3{0.51F, 0.42F, 0.55F}, Vec3{0.90F, 0.84F, 0.82F},
     Vec3{0.58F, 0.55F, 0.66F}, Vec3{0.97F, 0.96F, 1.00F}, Vec3{0.49F, 0.44F, 0.58F},
     Vec3{0.25F, 0.22F, 0.36F}},
    // Alpine: cool green, grey rock.
    {Vec3{0.42F, 0.64F, 0.42F}, Vec3{0.48F, 0.42F, 0.36F}, Vec3{0.82F, 0.80F, 0.70F},
     Vec3{0.66F, 0.68F, 0.72F}, Vec3{0.98F, 0.99F, 1.00F}, Vec3{0.58F, 0.60F, 0.66F},
     Vec3{0.29F, 0.31F, 0.40F}},
}};

// Per-island variation: brightness factor and tint per channel.
constexpr float kMinBrightness = 0.92F;
constexpr float kMaxBrightness = 1.08F;
constexpr float kMaxTint = 0.05F;

struct NamedColour {
    const char* name;
    const Vec3& colour;
};

std::array<NamedColour, 7> entriesOf(const IslandPalette& palette)
{
    return {{{"grass", palette.grass},
             {"soil", palette.soil},
             {"sand", palette.sand},
             {"rock", palette.rock},
             {"snow", palette.snow},
             {"stone", palette.stone},
             {"deepStone", palette.deepStone}}};
}

Vec3 tinted(const Vec3& colour, float brightness, const Vec3& tint)
{
    const Vec3 scaled = colour * brightness * (Vec3{1.0F} + tint);
    return glm::clamp(scaled, Vec3{0.0F}, Vec3{1.0F});
}

Vec3 colourOf(const IslandPalette& palette, TopSurface surface)
{
    switch (surface) {
    case TopSurface::Soil:
        return palette.soil;
    case TopSurface::Sand:
        return palette.sand;
    case TopSurface::Rock:
        return palette.rock;
    case TopSurface::Snow:
        return palette.snow;
    case TopSurface::Grass:
        break;
    }
    return palette.grass;
}

// Centre of triangle t of a flat-shaded mesh (its own three vertices).
glm::dvec3 centreOf(const geometry::MeshData& mesh, std::size_t t)
{
    glm::dvec3 sum{0.0};
    for (std::size_t corner = 0; corner < 3U; ++corner) {
        const Vec3& p = mesh.positions[mesh.indices[3U * t + corner]];
        sum += glm::dvec3{static_cast<double>(p.x), static_cast<double>(p.y),
                          static_cast<double>(p.z)};
    }
    return sum / 3.0;
}

void setFaceColour(geometry::MeshData& mesh, std::size_t t, const Vec3& colour)
{
    const math::Vec4 rgba{colour, 1.0F};
    for (std::size_t corner = 0; corner < 3U; ++corner) {
        mesh.colors[mesh.indices[3U * t + corner]] = rgba;
    }
}

} // namespace

std::span<const IslandPalette> builtInPalettes()
{
    return kPalettes;
}

std::optional<core::Error> validate(const IslandPalette& palette)
{
    constexpr std::array<const char*, 3> channels{"r", "g", "b"};
    for (const NamedColour& entry : entriesOf(palette)) {
        for (int channel = 0; channel < 3; ++channel) {
            const float value = entry.colour[channel];
            if (!(value >= 0.0F && value <= 1.0F)) {
                return core::Error{std::format("{}.{} is {:g}, outside 0 to 1", entry.name,
                                               channels[static_cast<std::size_t>(channel)], value)};
            }
        }
    }
    return std::nullopt;
}

TopSurface classifyTop(float elevation, float normalY, float rimDistance)
{
    if (normalY < kRockSlope) {
        return TopSurface::Rock;
    }
    if (elevation >= kSnowLine) {
        return TopSurface::Snow;
    }
    if (rimDistance >= kBeachDistance && elevation < kBeachHeight) {
        return TopSurface::Sand;
    }
    if (normalY < kSoilSlope) {
        return TopSurface::Soil;
    }
    return TopSurface::Grass;
}

Vec3 stoneStratum(const IslandPalette& palette, int stratum)
{
    const int clamped = std::clamp(stratum, 0, kStoneStrata - 1);
    const float t = static_cast<float>(clamped) / static_cast<float>(kStoneStrata - 1);
    return palette.stone + (palette.deepStone - palette.stone) * t;
}

std::vector<ChosenPalette> choosePalettes(const IslandLayout& layout,
                                          const WorldGenSettings& settings)
{
    const auto paletteCount = static_cast<std::uint32_t>(kPalettes.size());
    std::vector<ChosenPalette> chosen;
    chosen.reserve(layout.islands.size());
    for (std::size_t i = 0; i < layout.islands.size(); ++i) {
        const PlacedIsland& island = layout.islands[i];
        Pcg32 rng{deriveSeed(settings.seed, RandomStream::IslandPalette, island.index,
                             settings.generatorVersion)};
        // Always the same draws in the same order.
        std::uint32_t base = rng.nextBounded(paletteCount);
        const std::uint32_t alternative = rng.nextBounded(paletteCount - 1U);
        const float brightness = rng.nextFloat(kMinBrightness, kMaxBrightness);
        const float tintR = rng.nextFloat(-kMaxTint, kMaxTint);
        const float tintG = rng.nextFloat(-kMaxTint, kMaxTint);
        const float tintB = rng.nextFloat(-kMaxTint, kMaxTint);

        // The nearest island placed before this one (the first on ties).
        std::size_t nearest = i;
        double nearestDistance = std::numeric_limits<double>::max();
        for (std::size_t j = 0; j < i; ++j) {
            const math::Vec3& a = island.anchor;
            const math::Vec3& b = layout.islands[j].anchor;
            const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
            const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
            const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
            const double distance = dx * dx + dy * dy + dz * dz;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = j;
            }
        }
        if (nearest < i && chosen[nearest].basePalette == base) {
            base = (base + 1U + alternative) % paletteCount;
        }

        const IslandPalette& source = kPalettes[base];
        const Vec3 tint{tintR, tintG, tintB};
        ChosenPalette result;
        result.basePalette = base;
        result.palette = IslandPalette{
            tinted(source.grass, brightness, tint),    tinted(source.soil, brightness, tint),
            tinted(source.sand, brightness, tint),     tinted(source.rock, brightness, tint),
            tinted(source.snow, brightness, tint),     tinted(source.stone, brightness, tint),
            tinted(source.deepStone, brightness, tint)};
        chosen.push_back(result);
    }
    return chosen;
}

void paintIsland(IslandMesh& mesh, const IslandShape& shape, const IslandPalette& palette)
{
    const auto topHeight = static_cast<double>(shape.island.topHeight);
    const auto undersideDepth = static_cast<double>(shape.island.undersideDepth);

    geometry::MeshData& top = mesh.top;
    top.colors.resize(top.positions.size(), math::Vec4{1.0F});
    for (std::size_t t = 0; t < top.triangleCount(); ++t) {
        const glm::dvec3 centre = centreOf(top, t);
        const auto elevation = static_cast<float>(centre.y / topHeight);
        const float normalY = top.normals.empty() ? 1.0F : top.normals[top.indices[3U * t]].y;
        const float rimDistance = outlineDistance(
            shape, math::Vec2{static_cast<float>(centre.x), static_cast<float>(centre.z)});
        setFaceColour(top, t, colourOf(palette, classifyTop(elevation, normalY, rimDistance)));
    }

    geometry::MeshData& underside = mesh.underside;
    underside.colors.resize(underside.positions.size(), math::Vec4{1.0F});
    for (std::size_t t = 0; t < underside.triangleCount(); ++t) {
        const double depthFraction = -centreOf(underside, t).y / undersideDepth;
        const auto stratum =
            static_cast<int>(std::floor(std::clamp(depthFraction, 0.0, 1.0) * kStoneStrata));
        setFaceColour(underside, t, stoneStratum(palette, stratum));
    }
}

} // namespace engine::worldgen
