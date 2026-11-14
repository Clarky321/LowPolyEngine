// Placement of the islands in 3D (ADR 0013, Amendment 1).
//
// Golden layouts were computed with an independent implementation of the
// documented placement; they are exact float bit patterns, because placement
// uses only correctly rounded IEEE operations. The geometric checks below use
// their own formulas (with a square root) rather than the placement's.
#include "engine/worldgen/IslandLayout.hpp"

#include "engine/worldgen/Random.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

using engine::worldgen::deriveSeed;
using engine::worldgen::FloatRange;
using engine::worldgen::IslandLayout;
using engine::worldgen::kGeneratorVersion;
using engine::worldgen::PlacedIsland;
using engine::worldgen::placeIslands;
using engine::worldgen::RandomStream;
using engine::worldgen::WorldGenSettings;

namespace {

constexpr double kTolerance = 1.0e-3;

IslandLayout layoutOf(const WorldGenSettings& settings)
{
    auto result = placeIslands(settings);
    REQUIRE(result.hasValue());
    return result.value();
}

WorldGenSettings withSeed(std::uint64_t seed)
{
    WorldGenSettings settings;
    settings.seed = seed;
    return settings;
}

// More and smaller islands, a lower and wider altitude range, larger spacings.
WorldGenSettings crowdedSettings(std::uint64_t seed)
{
    WorldGenSettings settings = withSeed(seed);
    settings.islands.count = 40;
    settings.islands.radius = FloatRange{6.0F, 30.0F};
    settings.islands.altitude = FloatRange{-40.0F, 120.0F};
    settings.islands.minHorizontalSpacing = 25.0F;
    settings.islands.minVerticalSpacing = 20.0F;
    return settings;
}

std::uint32_t bitsOf(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

struct GoldenIsland {
    std::uint32_t index;
    std::array<std::uint32_t, 6> bits; // anchor x, y, z, radius, top height, underside depth
    std::uint64_t shapeSeed;
};

void checkGolden(const PlacedIsland& island, const GoldenIsland& golden)
{
    CHECK(island.index == golden.index);
    CHECK(bitsOf(island.anchor.x) == golden.bits[0]);
    CHECK(bitsOf(island.anchor.y) == golden.bits[1]);
    CHECK(bitsOf(island.anchor.z) == golden.bits[2]);
    CHECK(bitsOf(island.radius) == golden.bits[3]);
    CHECK(bitsOf(island.topHeight) == golden.bits[4]);
    CHECK(bitsOf(island.undersideDepth) == golden.bits[5]);
    CHECK(island.shapeSeed == golden.shapeSeed);
}

double horizontalGap(const PlacedIsland& a, const PlacedIsland& b)
{
    const double dx = static_cast<double>(a.anchor.x) - static_cast<double>(b.anchor.x);
    const double dz = static_cast<double>(a.anchor.z) - static_cast<double>(b.anchor.z);
    return std::sqrt(dx * dx + dz * dz) - static_cast<double>(a.radius) -
           static_cast<double>(b.radius);
}

double verticalGap(const PlacedIsland& a, const PlacedIsland& b)
{
    const double aLow = static_cast<double>(a.anchor.y) - static_cast<double>(a.undersideDepth);
    const double aHigh = static_cast<double>(a.anchor.y) + static_cast<double>(a.topHeight);
    const double bLow = static_cast<double>(b.anchor.y) - static_cast<double>(b.undersideDepth);
    const double bHigh = static_cast<double>(b.anchor.y) + static_cast<double>(b.topHeight);
    return std::max(bLow - aHigh, aLow - bHigh);
}

// Counts the islands outside the bounds or the altitude range, with volumes
// that do not match the settings, and pairs that keep neither spacing.
struct Violations {
    int outsideBounds = 0;
    int wrongVolume = 0;
    int tooClose = 0;
};

Violations violationsOf(const WorldGenSettings& settings, const IslandLayout& layout)
{
    Violations violations;
    const auto& bounds = settings.bounds;
    const auto lowLimit = [](float limit) { return static_cast<double>(limit) - kTolerance; };
    const auto highLimit = [](float limit) { return static_cast<double>(limit) + kTolerance; };
    for (const PlacedIsland& island : layout.islands) {
        const auto x = static_cast<double>(island.anchor.x);
        const auto y = static_cast<double>(island.anchor.y);
        const auto z = static_cast<double>(island.anchor.z);
        const auto radius = static_cast<double>(island.radius);
        const bool inside =
            x - radius >= lowLimit(bounds.min.x) && x + radius <= highLimit(bounds.max.x) &&
            z - radius >= lowLimit(bounds.min.z) && z + radius <= highLimit(bounds.max.z) &&
            y - static_cast<double>(island.undersideDepth) >= lowLimit(bounds.min.y) &&
            y + static_cast<double>(island.topHeight) <= highLimit(bounds.max.y) &&
            y >= lowLimit(settings.islands.altitude.min) &&
            y <= highLimit(settings.islands.altitude.max);
        if (!inside) {
            ++violations.outsideBounds;
        }
        const float depthRatio = island.undersideDepth / island.radius;
        const bool volumeMatches =
            island.radius >= settings.islands.radius.min &&
            island.radius <= settings.islands.radius.max &&
            island.topHeight == settings.elevation.maxHeightRatio * island.radius &&
            depthRatio >= settings.underside.depthRatio.min - 1.0e-5F &&
            depthRatio <= settings.underside.depthRatio.max + 1.0e-5F &&
            island.shapeSeed == deriveSeed(settings.seed, RandomStream::IslandShape, island.index,
                                           settings.generatorVersion);
        if (!volumeMatches) {
            ++violations.wrongVolume;
        }
    }
    const auto& islands = layout.islands;
    const auto minHorizontal = static_cast<double>(settings.islands.minHorizontalSpacing);
    const auto minVertical = static_cast<double>(settings.islands.minVerticalSpacing);
    for (std::size_t i = 0; i < islands.size(); ++i) {
        for (std::size_t j = i + 1; j < islands.size(); ++j) {
            if (horizontalGap(islands[i], islands[j]) < minHorizontal - kTolerance &&
                verticalGap(islands[i], islands[j]) < minVertical - kTolerance) {
                ++violations.tooClose;
            }
        }
    }
    return violations;
}

} // namespace

TEST_CASE("placeIslands matches its golden layouts", "[worldgen][placement]")
{
    const IslandLayout first = layoutOf(withSeed(1U));
    REQUIRE(first.islands.size() == 12U);
    CHECK(first.requestedCount == 12);
    CHECK(first.warning.empty());
    checkGolden(first.islands[0],
                {0U,
                 {0xC3292420U, 0x41EE9DFCU, 0x42E272D7U, 0x420A17AFU, 0x415CF2B2U, 0x41C4C79FU},
                 0xC817A9BF84F9A191ULL});
    checkGolden(first.islands[1],
                {1U,
                 {0x42943282U, 0xC1923EF4U, 0x41B7828CU, 0x423EAA2CU, 0x41988823U, 0x422EAEC3U},
                 0x6C8DB57FBC06F37EULL});
    checkGolden(first.islands[2],
                {2U,
                 {0x43436343U, 0x425CF36BU, 0x42BACDB3U, 0x42208E27U, 0x418071B9U, 0x41C38986U},
                 0x11E14D08FADCAA61ULL});

    const IslandLayout second = layoutOf(withSeed(42U));
    REQUIRE(second.islands.size() == 12U);
    checkGolden(second.islands[0],
                {0U,
                 {0x433E03C8U, 0x42C95F74U, 0xC05C0445U, 0x41BB8615U, 0x411604DEU, 0x417960C6U},
                 0xF4A98999ACF5DBF1ULL});
    checkGolden(second.islands[1],
                {1U,
                 {0x4252F7E4U, 0x42C066F2U, 0x4201BEF4U, 0x41820076U, 0x40D000BDU, 0x416A8F1CU},
                 0xF37FF738CD055B2FULL});
    checkGolden(second.islands[2],
                {2U,
                 {0xC213B296U, 0xC1885DECU, 0xC2334A09U, 0x42437F0BU, 0x419C65A3U, 0x422FB408U},
                 0x627529AD11B645F2ULL});
}

TEST_CASE("Islands that do not fit are left out with a warning", "[worldgen][placement]")
{
    WorldGenSettings settings = withSeed(42U);
    settings.islands.count = 40;
    settings.islands.radius = FloatRange{50.0F, 55.0F};
    settings.islands.minHorizontalSpacing = 60.0F;
    settings.islands.minVerticalSpacing = 60.0F;
    const IslandLayout layout = layoutOf(settings);

    CHECK(layout.requestedCount == 40);
    std::vector<std::uint32_t> indices;
    for (const PlacedIsland& island : layout.islands) {
        indices.push_back(island.index);
    }
    CHECK(indices == std::vector<std::uint32_t>{0U, 1U, 2U, 3U, 4U, 5U, 11U, 15U});
    CHECK(layout.warning == "placed 8 of 40 islands: 32 found no position that keeps the "
                            "spacing in 100 attempts each");
    const Violations violations = violationsOf(settings, layout);
    CHECK(violations.outsideBounds == 0);
    CHECK(violations.wrongVolume == 0);
    CHECK(violations.tooClose == 0);
}

TEST_CASE("Islands stack vertically when there is no room beside each other",
          "[worldgen][placement]")
{
    // The bounds are exactly as wide as an island: every island stands at the
    // same X and Z and must keep the vertical spacing.
    WorldGenSettings settings = withSeed(7U);
    settings.bounds.min.x = -55.0F;
    settings.bounds.max.x = 55.0F;
    settings.bounds.min.z = -55.0F;
    settings.bounds.max.z = 55.0F;
    settings.islands.radius = FloatRange{55.0F, 55.0F};
    settings.islands.count = 64;
    const IslandLayout layout = layoutOf(settings);

    REQUIRE_FALSE(layout.islands.empty());
    CHECK(layout.islands.size() < 64U);
    CHECK_FALSE(layout.warning.empty());
    for (const PlacedIsland& island : layout.islands) {
        CHECK(island.anchor.x == 0.0F);
        CHECK(island.anchor.z == 0.0F);
    }
    const Violations violations = violationsOf(settings, layout);
    CHECK(violations.outsideBounds == 0);
    CHECK(violations.tooClose == 0);
}

TEST_CASE("Layouts of a seed corpus keep bounds, volumes and spacing", "[worldgen][placement]")
{
    int incomplete = 0;
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        for (const WorldGenSettings& settings : {withSeed(seed), crowdedSettings(seed)}) {
            INFO("seed " << seed << ", " << settings.islands.count << " islands requested");
            const IslandLayout layout = layoutOf(settings);
            const Violations violations = violationsOf(settings, layout);
            CHECK(violations.outsideBounds == 0);
            CHECK(violations.wrongVolume == 0);
            CHECK(violations.tooClose == 0);
            CHECK_FALSE(layout.islands.empty());
            CHECK(std::is_sorted(
                layout.islands.begin(), layout.islands.end(),
                [](const PlacedIsland& a, const PlacedIsland& b) { return a.index < b.index; }));
            CHECK(layout.warning.empty() ==
                  (layout.islands.size() == static_cast<std::size_t>(settings.islands.count)));
            if (!layout.warning.empty()) {
                ++incomplete;
            }
        }
    }
    // The defaults and the crowded settings leave room for every island.
    CHECK(incomplete == 0);
}

TEST_CASE("The same settings give the same layout", "[worldgen][placement]")
{
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        CHECK(layoutOf(withSeed(seed)) == layoutOf(withSeed(seed)));
        CHECK(layoutOf(crowdedSettings(seed)) == layoutOf(crowdedSettings(seed)));
    }
}

TEST_CASE("Altitudes and sizes vary", "[worldgen][placement]")
{
    float lowestRim = 1000.0F;
    float highestRim = -1000.0F;
    float smallest = 1000.0F;
    float largest = 0.0F;
    int flatWorlds = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        const IslandLayout layout = layoutOf(withSeed(seed));
        float worldLow = 1000.0F;
        float worldHigh = -1000.0F;
        for (const PlacedIsland& island : layout.islands) {
            lowestRim = std::min(lowestRim, island.anchor.y);
            highestRim = std::max(highestRim, island.anchor.y);
            smallest = std::min(smallest, island.radius);
            largest = std::max(largest, island.radius);
            worldLow = std::min(worldLow, island.anchor.y);
            worldHigh = std::max(worldHigh, island.anchor.y);
        }
        // Within one world the islands float at clearly different heights.
        if (worldHigh - worldLow < 40.0F) {
            ++flatWorlds;
        }
    }
    // Over the corpus the ranges -20..110 and 16..55 are used almost fully.
    CHECK(lowestRim < -15.0F);
    CHECK(highestRim > 105.0F);
    CHECK(smallest < 18.0F);
    CHECK(largest > 53.0F);
    CHECK(flatWorlds == 0);
}

TEST_CASE("Different seeds give different layouts", "[worldgen][placement]")
{
    std::set<std::uint32_t> firstPositions;
    IslandLayout previous = layoutOf(withSeed(0U));
    int same = 0;
    for (std::uint64_t seed = 1; seed < 200; ++seed) {
        const IslandLayout layout = layoutOf(withSeed(seed));
        firstPositions.insert(bitsOf(layout.islands.front().anchor.x));
        if (layout.islands == previous.islands) {
            ++same;
        }
        previous = layout;
    }
    CHECK(same == 0);
    CHECK(firstPositions.size() == 199U);
}

TEST_CASE("Every island gets its own shape seed", "[worldgen][placement]")
{
    std::set<std::uint64_t> seeds;
    std::size_t islandCount = 0;
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        const IslandLayout layout = layoutOf(crowdedSettings(seed));
        for (const PlacedIsland& island : layout.islands) {
            seeds.insert(island.shapeSeed);
            ++islandCount;
            CHECK(island.shapeSeed ==
                  deriveSeed(seed, RandomStream::IslandShape, island.index, kGeneratorVersion));
        }
    }
    CHECK(seeds.size() == islandCount);
}

TEST_CASE("A layout with more islands starts with the layout with fewer", "[worldgen][placement]")
{
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        WorldGenSettings fewer = withSeed(seed);
        fewer.islands.count = 6;
        const IslandLayout small = layoutOf(fewer);
        const IslandLayout large = layoutOf(withSeed(seed));
        REQUIRE(small.islands.size() == 6U);
        REQUIRE(large.islands.size() >= 6U);
        CHECK(std::equal(small.islands.begin(), small.islands.end(), large.islands.begin()));
    }
}

TEST_CASE("Invalid settings give validate's error", "[worldgen][placement]")
{
    WorldGenSettings settings;
    settings.islands.count = 0;
    const auto result = placeIslands(settings);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message == "islands.count is 0, outside its range 1 to 64");

    settings = WorldGenSettings{};
    settings.islands.altitude = FloatRange{100.0F, 50.0F};
    const auto inverted = placeIslands(settings);
    REQUIRE_FALSE(inverted.hasValue());
    CHECK(inverted.error().message.starts_with("islands.altitude.min"));
}
