// Island palettes and painted island meshes (ADR 0015).
#include "engine/worldgen/IslandPalette.hpp"

#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/IslandMesh.hpp"
#include "engine/worldgen/IslandShape.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

using engine::math::Vec3;
using engine::math::Vec4;
using engine::worldgen::buildIslandMesh;
using engine::worldgen::builtInPalettes;
using engine::worldgen::choosePalettes;
using engine::worldgen::ChosenPalette;
using engine::worldgen::classifyTop;
using engine::worldgen::IslandLayout;
using engine::worldgen::IslandMesh;
using engine::worldgen::IslandPalette;
using engine::worldgen::IslandShape;
using engine::worldgen::kStoneStrata;
using engine::worldgen::makeIslandShape;
using engine::worldgen::paintIsland;
using engine::worldgen::placeIslands;
using engine::worldgen::stoneStratum;
using engine::worldgen::TopSurface;
using engine::worldgen::WorldGenSettings;

namespace {

WorldGenSettings withSeed(std::uint64_t seed)
{
    WorldGenSettings settings;
    settings.seed = seed;
    return settings;
}

IslandLayout layoutOf(const WorldGenSettings& settings)
{
    auto layout = placeIslands(settings);
    REQUIRE(layout.hasValue());
    return layout.value();
}

std::uint32_t bitsOf(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

Vec4 opaque(const Vec3& colour)
{
    return Vec4{colour, 1.0F};
}

std::size_t nearestEarlier(const IslandLayout& layout, std::size_t i)
{
    std::size_t nearest = i;
    double best = std::numeric_limits<double>::max();
    for (std::size_t j = 0; j < i; ++j) {
        const Vec3 d = layout.islands[i].anchor - layout.islands[j].anchor;
        const auto x = static_cast<double>(d.x);
        const auto y = static_cast<double>(d.y);
        const auto z = static_cast<double>(d.z);
        const double distance = x * x + y * y + z * z;
        if (distance < best) {
            best = distance;
            nearest = j;
        }
    }
    return nearest;
}

} // namespace

TEST_CASE("Built-in palettes are valid and distinct", "[worldgen][palette]")
{
    const auto palettes = builtInPalettes();
    REQUIRE(palettes.size() == 6U);
    std::set<std::uint32_t> grasses;
    for (const IslandPalette& palette : palettes) {
        CHECK_FALSE(engine::worldgen::validate(palette).has_value());
        grasses.insert(bitsOf(palette.grass.r) ^ (bitsOf(palette.grass.g) << 1U));
        // The underside darkens towards the tip.
        CHECK(palette.deepStone.r < palette.stone.r);
        CHECK(palette.deepStone.g < palette.stone.g);
        CHECK(palette.deepStone.b < palette.stone.b);
    }
    CHECK(grasses.size() == palettes.size());
}

TEST_CASE("validate names the first colour outside [0, 1]", "[worldgen][palette]")
{
    IslandPalette palette = builtInPalettes()[0];
    palette.rock.g = 1.2F;
    palette.deepStone.b = -0.1F;
    auto error = engine::worldgen::validate(palette);
    REQUIRE(error.has_value());
    CHECK(error->message == "rock.g is 1.2, outside 0 to 1");

    palette = builtInPalettes()[0];
    palette.deepStone.b = -0.1F;
    error = engine::worldgen::validate(palette);
    REQUIRE(error.has_value());
    CHECK(error->message == "deepStone.b is -0.1, outside 0 to 1");

    palette = builtInPalettes()[0];
    palette.grass.r = std::numeric_limits<float>::quiet_NaN();
    error = engine::worldgen::validate(palette);
    REQUIRE(error.has_value());
    CHECK(error->message == "grass.r is nan, outside 0 to 1");

    // The limits themselves are valid.
    palette = builtInPalettes()[0];
    palette.snow = Vec3{1.0F};
    palette.deepStone = Vec3{0.0F};
    CHECK_FALSE(engine::worldgen::validate(palette).has_value());
}

TEST_CASE("Thresholds pick the expected top surface", "[worldgen][palette]")
{
    struct Case {
        float elevation;
        float normalY;
        float rimDistance;
        TopSurface expected;
    };
    const std::vector<Case> cases{
        // Steep faces are rock, also high up and at the rim.
        {0.5F, 0.6F, 0.5F, TopSurface::Rock},
        {0.95F, 0.69F, 0.2F, TopSurface::Rock},
        {0.05F, 0.5F, 0.95F, TopSurface::Rock},
        // High faces are snow.
        {0.9F, 0.95F, 0.3F, TopSurface::Snow},
        {0.82F, 0.75F, 0.3F, TopSurface::Snow},
        // Low faces near the rim are sand.
        {0.05F, 0.95F, 0.9F, TopSurface::Sand},
        {0.0F, 0.99F, 0.88F, TopSurface::Sand},
        // Moderate slopes are soil.
        {0.5F, 0.8F, 0.5F, TopSurface::Soil},
        {0.05F, 0.8F, 0.95F, TopSurface::Sand},
        // Everything else is grass.
        {0.5F, 0.9F, 0.5F, TopSurface::Grass},
        {0.05F, 0.95F, 0.87F, TopSurface::Grass},
        {0.1F, 0.95F, 0.95F, TopSurface::Grass},
        // Boundaries: normal.y 0.7 is not rock, 0.86 is not soil, elevation
        // 0.82 is snow, rim distance 0.88 is sand.
        {0.5F, 0.7F, 0.5F, TopSurface::Soil},
        {0.5F, 0.86F, 0.5F, TopSurface::Grass},
        {0.82F, 0.9F, 0.5F, TopSurface::Snow},
        {0.81F, 0.9F, 0.5F, TopSurface::Grass},
        {0.05F, 0.9F, 0.88F, TopSurface::Sand},
    };
    for (const Case& c : cases) {
        INFO("elevation " << c.elevation << ", normal.y " << c.normalY << ", rim distance "
                          << c.rimDistance);
        CHECK(classifyTop(c.elevation, c.normalY, c.rimDistance) == c.expected);
    }
}

TEST_CASE("Stone strata run from stone to deep stone", "[worldgen][palette]")
{
    const IslandPalette& palette = builtInPalettes()[2];
    CHECK(stoneStratum(palette, 0) == palette.stone);
    CHECK(stoneStratum(palette, kStoneStrata - 1) == palette.deepStone);
    CHECK(stoneStratum(palette, -3) == palette.stone);
    CHECK(stoneStratum(palette, 99) == palette.deepStone);
    const Vec3 middle = stoneStratum(palette, 1);
    CHECK(middle.r < palette.stone.r);
    CHECK(middle.r > palette.deepStone.r);
}

TEST_CASE("Palette choice matches its golden values", "[worldgen][palette]")
{
    // From an independent implementation of the documented choice.
    const WorldGenSettings settings = withSeed(42U);
    const std::vector<ChosenPalette> chosen = choosePalettes(layoutOf(settings), settings);
    REQUIRE(chosen.size() == 12U);
    std::vector<std::size_t> bases;
    for (const ChosenPalette& palette : chosen) {
        bases.push_back(palette.basePalette);
    }
    CHECK(bases == std::vector<std::size_t>{4U, 2U, 4U, 3U, 1U, 3U, 1U, 5U, 1U, 0U, 5U, 1U});
    CHECK(bitsOf(chosen[0].palette.grass.r) == 0x3F2817A6U);
    CHECK(bitsOf(chosen[0].palette.grass.g) == 0x3F1002B5U);
    CHECK(bitsOf(chosen[0].palette.grass.b) == 0x3F599AB2U);
    CHECK(bitsOf(chosen[5].palette.grass.r) == 0x3F4D05C3U);
    CHECK(bitsOf(chosen[5].palette.grass.g) == 0x3F26CFC3U);
    CHECK(bitsOf(chosen[5].palette.grass.b) == 0x3EDD5E22U);
}

TEST_CASE("Neighbours get different palettes and every island its own colours",
          "[worldgen][palette]")
{
    std::set<std::size_t> basesUsed;
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> grasses;
    std::size_t islands = 0;
    int sameAsNeighbour = 0;
    int dullWorlds = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        const WorldGenSettings settings = withSeed(seed);
        const IslandLayout layout = layoutOf(settings);
        const std::vector<ChosenPalette> chosen = choosePalettes(layout, settings);
        REQUIRE(chosen.size() == layout.islands.size());
        std::set<std::size_t> worldBases;
        for (std::size_t i = 0; i < chosen.size(); ++i) {
            CHECK_FALSE(engine::worldgen::validate(chosen[i].palette).has_value());
            basesUsed.insert(chosen[i].basePalette);
            worldBases.insert(chosen[i].basePalette);
            const Vec3& grass = chosen[i].palette.grass;
            grasses.insert({bitsOf(grass.r), bitsOf(grass.g), bitsOf(grass.b)});
            ++islands;
            if (i > 0U && chosen[nearestEarlier(layout, i)].basePalette == chosen[i].basePalette) {
                ++sameAsNeighbour;
            }
        }
        if (worldBases.size() < 3U) {
            ++dullWorlds;
        }
    }
    CHECK(sameAsNeighbour == 0);
    CHECK(basesUsed.size() == builtInPalettes().size());
    CHECK(dullWorlds == 0);
    // The tint gives every island of the corpus colours of its own.
    CHECK(grasses.size() == islands);
}

TEST_CASE("Palette choice is deterministic and has its own stream", "[worldgen][palette]")
{
    const WorldGenSettings settings = withSeed(17U);
    const IslandLayout layout = layoutOf(settings);
    CHECK(choosePalettes(layout, settings) == choosePalettes(layout, settings));

    // Shape settings do not move islands, so they do not change the palettes.
    WorldGenSettings reshaped = settings;
    reshaped.shape.outlineRoughness = 0.9F;
    reshaped.underside.ledgeCount = 5;
    CHECK(choosePalettes(layoutOf(reshaped), reshaped) == choosePalettes(layout, settings));
}

TEST_CASE("Painted islands use only their palette's colours", "[worldgen][palette]")
{
    std::set<TopSurface> surfaces;
    std::set<int> strata;
    for (std::uint64_t seed = 0; seed < 4; ++seed) {
        const WorldGenSettings settings = withSeed(seed);
        const IslandLayout layout = layoutOf(settings);
        const std::vector<ChosenPalette> chosen = choosePalettes(layout, settings);
        for (std::size_t i = 0; i < layout.islands.size(); ++i) {
            INFO("seed " << seed << " island " << layout.islands[i].index);
            const IslandShape shape = makeIslandShape(layout.islands[i], settings);
            IslandMesh mesh = buildIslandMesh(shape, settings.mesh.resolution);
            const IslandPalette& palette = chosen[i].palette;
            paintIsland(mesh, shape, palette);

            const std::vector<std::pair<TopSurface, Vec4>> topColours{
                {TopSurface::Grass, opaque(palette.grass)},
                {TopSurface::Soil, opaque(palette.soil)},
                {TopSurface::Sand, opaque(palette.sand)},
                {TopSurface::Rock, opaque(palette.rock)},
                {TopSurface::Snow, opaque(palette.snow)}};
            int foreignTop = 0;
            int splitFaces = 0;
            for (std::size_t t = 0; t < mesh.top.triangleCount(); ++t) {
                const Vec4& colour = mesh.top.colors[3U * t];
                if (colour != mesh.top.colors[3U * t + 1U] ||
                    colour != mesh.top.colors[3U * t + 2U]) {
                    ++splitFaces;
                }
                bool known = false;
                for (const auto& [surface, expected] : topColours) {
                    if (colour == expected) {
                        surfaces.insert(surface);
                        known = true;
                    }
                }
                foreignTop += known ? 0 : 1;
            }
            CHECK(foreignTop == 0);
            CHECK(splitFaces == 0);

            // The underside uses the stone strata, deeper faces darker strata.
            int foreignUnderside = 0;
            int wrongStratum = 0;
            for (std::size_t t = 0; t < mesh.underside.triangleCount(); ++t) {
                const Vec4& colour = mesh.underside.colors[3U * t];
                double depth = 0.0;
                for (std::size_t corner = 0; corner < 3U; ++corner) {
                    depth -= static_cast<double>(mesh.underside.positions[3U * t + corner].y);
                }
                depth /= 3.0 * static_cast<double>(shape.island.undersideDepth);
                const int expected =
                    std::min(static_cast<int>(std::floor(depth * kStoneStrata)), kStoneStrata - 1);
                bool known = false;
                for (int k = 0; k < kStoneStrata; ++k) {
                    if (colour == opaque(stoneStratum(palette, k))) {
                        known = true;
                        strata.insert(k);
                        wrongStratum += k == expected ? 0 : 1;
                    }
                }
                foreignUnderside += known ? 0 : 1;
            }
            CHECK(foreignUnderside == 0);
            CHECK(wrongStratum == 0);
        }
    }
    // Over the corpus every surface and every stratum occurs.
    CHECK(surfaces.size() == 5U);
    CHECK(strata.size() == static_cast<std::size_t>(kStoneStrata));
}

TEST_CASE("Painting is deterministic", "[worldgen][palette]")
{
    const WorldGenSettings settings = withSeed(23U);
    const IslandLayout layout = layoutOf(settings);
    const ChosenPalette chosen = choosePalettes(layout, settings).front();
    const IslandShape shape = makeIslandShape(layout.islands.front(), settings);
    IslandMesh first = buildIslandMesh(shape, settings.mesh.resolution);
    IslandMesh second = buildIslandMesh(shape, settings.mesh.resolution);
    paintIsland(first, shape, chosen.palette);
    paintIsland(second, shape, chosen.palette);
    CHECK(first.top.colors == second.top.colors);
    CHECK(first.underside.colors == second.underside.colors);
    // Painting changes colours only.
    CHECK(first.top.positions == second.top.positions);
    CHECK_FALSE(engine::geometry::validate(first.top).has_value());
    CHECK_FALSE(engine::geometry::validate(first.underside).has_value());
}
