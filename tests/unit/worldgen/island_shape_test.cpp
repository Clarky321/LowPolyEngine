// The shape of one island: outline, top surface and underside (ADR 0014).
//
// Golden samples were computed with an independent implementation of the
// documented formulas and are compared with a small tolerance. The shape
// criteria (not a flat disc, not a plain inverted cone) are measured here with
// the thresholds ADR 0014 fixes, over a corpus of seeds.
#include "engine/worldgen/IslandShape.hpp"

#include "engine/worldgen/IslandLayout.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

using engine::math::Vec2;
using engine::worldgen::islandMask;
using engine::worldgen::IslandShape;
using engine::worldgen::makeIslandShape;
using engine::worldgen::outlineDistance;
using engine::worldgen::outlineRadius;
using engine::worldgen::PlacedIsland;
using engine::worldgen::placeIslands;
using engine::worldgen::topHeightAt;
using engine::worldgen::undersideDepthAt;
using engine::worldgen::WorldGenSettings;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Unit direction number i of count around the circle (test code may use
// trigonometry; generation code does not).
Vec2 direction(int i, int count)
{
    const double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(count);
    return Vec2{static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}

// The point at normalized distance s along a direction.
Vec2 pointAt(const IslandShape& shape, Vec2 dir, double s)
{
    const double radius = static_cast<double>(outlineRadius(shape, dir)) * s;
    return Vec2{static_cast<float>(static_cast<double>(dir.x) * radius),
                static_cast<float>(static_cast<double>(dir.y) * radius)};
}

std::vector<IslandShape> shapesOf(const WorldGenSettings& settings)
{
    auto layout = placeIslands(settings);
    REQUIRE(layout.hasValue());
    std::vector<IslandShape> shapes;
    for (const PlacedIsland& island : layout.value().islands) {
        shapes.push_back(makeIslandShape(island, settings));
    }
    return shapes;
}

WorldGenSettings withSeed(std::uint64_t seed)
{
    WorldGenSettings settings;
    settings.seed = seed;
    return settings;
}

std::uint32_t bitsOf(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

bool near(float actual, float expected)
{
    return std::abs(actual - expected) <= 1.0e-4F * std::max(1.0F, std::abs(expected));
}

// Measurements of one underside, each relative to the island's undersideDepth.
struct UndersideMeasures {
    double meanDepth = 0.0;     // over the footprint
    double deepest = 0.0;       // over the footprint
    double around = 0.0;        // largest spread over directions on rings s = 0.3, 0.5, 0.7
    double coneDeviation = 0.0; // of the direction-averaged profile from a straight line
    double shelves = 0.0;       // flat runs per direction between s = 0.075 and 0.95
    double outlineRatio = 0.0;  // largest over smallest outline radius
};

UndersideMeasures measure(const IslandShape& shape)
{
    UndersideMeasures result;
    const auto depthScale = static_cast<double>(shape.island.undersideDepth);
    const auto radius = static_cast<double>(shape.island.radius);

    double sum = 0.0;
    int inside = 0;
    for (int a = -20; a <= 20; ++a) {
        for (int b = -20; b <= 20; ++b) {
            const Vec2 local{static_cast<float>(radius * a / 20.0),
                             static_cast<float>(radius * b / 20.0)};
            if (outlineDistance(shape, local) < 1.0F) {
                const auto depth = static_cast<double>(undersideDepthAt(shape, local));
                sum += depth;
                result.deepest = std::max(result.deepest, depth / depthScale);
                ++inside;
            }
        }
    }
    result.meanDepth = sum / static_cast<double>(inside) / depthScale;

    for (const double s : {0.3, 0.5, 0.7}) {
        double low = std::numeric_limits<double>::max();
        double high = 0.0;
        for (int i = 0; i < 64; ++i) {
            const auto depth =
                static_cast<double>(undersideDepthAt(shape, pointAt(shape, direction(i, 64), s)));
            low = std::min(low, depth);
            high = std::max(high, depth);
        }
        result.around = std::max(result.around, (high - low) / depthScale);
    }

    constexpr int samples = 40;
    std::vector<double> profile(static_cast<std::size_t>(samples) + 1U, 0.0);
    int shelfCount = 0;
    for (int i = 0; i < 64; ++i) {
        const Vec2 dir = direction(i, 64);
        double previous = 0.0;
        bool inShelf = false;
        for (int j = 0; j <= samples; ++j) {
            const double s = static_cast<double>(j) / samples;
            const auto depth = static_cast<double>(undersideDepthAt(shape, pointAt(shape, dir, s)));
            profile[static_cast<std::size_t>(j)] += depth / 64.0;
            if (j >= 3 && j <= 38) {
                const bool flat = std::abs(depth - previous) < 0.01 * depthScale;
                if (flat && !inShelf) {
                    ++shelfCount;
                }
                inShelf = flat;
            }
            previous = depth;
        }
    }
    result.shelves = static_cast<double>(shelfCount) / 64.0;
    for (int j = 0; j <= samples; ++j) {
        const double s = static_cast<double>(j) / samples;
        const double cone = profile[0] * (1.0 - s);
        result.coneDeviation =
            std::max(result.coneDeviation,
                     std::abs(profile[static_cast<std::size_t>(j)] - cone) / depthScale);
    }

    double smallest = std::numeric_limits<double>::max();
    double largest = 0.0;
    for (int i = 0; i < 128; ++i) {
        const auto r = static_cast<double>(outlineRadius(shape, direction(i, 128)));
        smallest = std::min(smallest, r);
        largest = std::max(largest, r);
    }
    result.outlineRatio = largest / smallest;
    return result;
}

} // namespace

TEST_CASE("Island shapes match their golden samples", "[worldgen][shape]")
{
    const std::vector<IslandShape> shapes = shapesOf(withSeed(42U));
    REQUIRE(shapes.size() == 12U);

    struct Sample {
        Vec2 local;
        float top;
        float depth;
    };
    struct Golden {
        std::size_t island;
        bool terraced;
        std::vector<float> outline; // along (1, 0), (0, 1), (-0.6, 0.8)
        std::vector<Sample> samples;
    };
    const std::vector<Golden> goldens{
        {0U,
         true,
         {20.5684414F, 17.4708519F, 17.9182434F},
         {{{0.0F, 0.0F}, 6.25079203F, 15.5861263F},
          {{3.0F, -2.0F}, 6.25079203F, 12.0030785F},
          {{-9.5F, 7.0F}, 3.12539601F, 6.17950535F},
          {{11.5F, -4.75F}, 3.12539601F, 7.44997931F}}},
        {2U,
         false,
         {44.1758919F, 42.4025574F, 42.3975449F},
         {{{0.0F, 0.0F}, 9.74020863F, 43.9258118F},
          {{3.0F, -2.0F}, 10.3763199F, 34.4774551F},
          {{-19.5F, 14.5F}, 11.8760004F, 23.5051937F},
          {{24.25F, -9.75F}, 11.1528654F, 25.0717392F}}},
    };
    const std::vector<Vec2> directions{{1.0F, 0.0F}, {0.0F, 1.0F}, {-0.6F, 0.8F}};
    for (const Golden& golden : goldens) {
        INFO("island " << golden.island);
        const IslandShape& shape = shapes[golden.island];
        CHECK(shape.terraced == golden.terraced);
        for (std::size_t i = 0; i < directions.size(); ++i) {
            CHECK(near(outlineRadius(shape, directions[i]), golden.outline[i]));
        }
        for (const Sample& sample : golden.samples) {
            INFO("at (" << sample.local.x << ", " << sample.local.y << ")");
            CHECK(near(topHeightAt(shape, sample.local), sample.top));
            CHECK(near(undersideDepthAt(shape, sample.local), sample.depth));
        }
    }
}

TEST_CASE("Outlines stay inside the bounding circle", "[worldgen][shape]")
{
    int outside = 0;
    int tooSmall = 0;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        for (const IslandShape& shape : shapesOf(withSeed(seed))) {
            for (int i = 0; i < 128; ++i) {
                const float r = outlineRadius(shape, direction(i, 128));
                if (!(r > 0.0F && r <= shape.island.radius)) {
                    ++outside;
                }
                // Ellipticity and outline noise never shrink an island to a sliver.
                if (r < 0.25F * shape.island.radius) {
                    ++tooSmall;
                }
            }
        }
    }
    CHECK(outside == 0);
    CHECK(tooSmall == 0);
}

TEST_CASE("Heights and depths stay in their bounds", "[worldgen][shape]")
{
    int outOfBounds = 0;
    int notFinite = 0;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        for (const IslandShape& shape : shapesOf(withSeed(seed))) {
            const auto radius = static_cast<double>(shape.island.radius);
            for (int a = -16; a <= 16; ++a) {
                for (int b = -16; b <= 16; ++b) {
                    const Vec2 local{static_cast<float>(radius * a / 15.0),
                                     static_cast<float>(radius * b / 15.0)};
                    const float mask = islandMask(shape, local);
                    const float top = topHeightAt(shape, local);
                    const float depth = undersideDepthAt(shape, local);
                    if (!std::isfinite(mask) || !std::isfinite(top) || !std::isfinite(depth)) {
                        ++notFinite;
                    }
                    if (!(mask >= 0.0F && mask <= 1.0F && top >= 0.0F &&
                          top <= shape.island.topHeight && depth >= 0.0F &&
                          depth <= shape.island.undersideDepth)) {
                        ++outOfBounds;
                    }
                }
            }
        }
    }
    CHECK(notFinite == 0);
    CHECK(outOfBounds == 0);
}

TEST_CASE("Mask, top and underside are zero outside the footprint", "[worldgen][shape]")
{
    int nonZeroOutside = 0;
    int notFullInside = 0;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        for (const IslandShape& shape : shapesOf(withSeed(seed))) {
            for (int i = 0; i < 64; ++i) {
                const Vec2 dir = direction(i, 64);
                for (const double s : {1.0001, 1.1, 1.5, 3.0}) {
                    const Vec2 local = pointAt(shape, dir, s);
                    if (islandMask(shape, local) != 0.0F || topHeightAt(shape, local) != 0.0F ||
                        undersideDepthAt(shape, local) != 0.0F) {
                        ++nonZeroOutside;
                    }
                }
                // Beyond the bounding circle in any case.
                const Vec2 far{dir.x * shape.island.radius * 1.01F,
                               dir.y * shape.island.radius * 1.01F};
                if (islandMask(shape, far) != 0.0F || topHeightAt(shape, far) != 0.0F ||
                    undersideDepthAt(shape, far) != 0.0F) {
                    ++nonZeroOutside;
                }
                // The interior inside the falloff has the full mask.
                if (islandMask(shape, pointAt(shape, dir, 0.7)) != 1.0F) {
                    ++notFullInside;
                }
            }
        }
    }
    CHECK(nonZeroOutside == 0);
    CHECK(notFullInside == 0);
}

TEST_CASE("Top and underside meet at the rim", "[worldgen][shape]")
{
    int apart = 0;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        for (const IslandShape& shape : shapesOf(withSeed(seed))) {
            for (int i = 0; i < 64; ++i) {
                const Vec2 justInside = pointAt(shape, direction(i, 64), 0.9999);
                if (topHeightAt(shape, justInside) > 0.005F * shape.island.topHeight ||
                    undersideDepthAt(shape, justInside) > 0.005F * shape.island.undersideDepth) {
                    ++apart;
                }
            }
        }
    }
    CHECK(apart == 0);
}

TEST_CASE("Undersides are neither flat discs nor plain inverted cones", "[worldgen][shape]")
{
    // Thresholds from ADR 0014; the corpus minimums are well above them.
    int failures = 0;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        for (const IslandShape& shape : shapesOf(withSeed(seed))) {
            const UndersideMeasures m = measure(shape);
            INFO("seed " << seed << " island " << shape.island.index << ": mean " << m.meanDepth
                         << " deepest " << m.deepest << " around " << m.around << " cone "
                         << m.coneDeviation << " shelves " << m.shelves << " outline "
                         << m.outlineRatio);
            const bool ok = m.meanDepth >= 0.25 && m.deepest >= 0.8 && m.around >= 0.08 &&
                            m.coneDeviation >= 0.08 && m.shelves >= 2.0 && m.outlineRatio >= 1.05;
            CHECK(ok);
            if (!ok) {
                ++failures;
            }
        }
    }
    CHECK(failures == 0);
}

TEST_CASE("Without variation settings an island is round and the same all around",
          "[worldgen][shape]")
{
    WorldGenSettings settings = withSeed(5U);
    settings.shape.ellipticity = 0.0F;
    settings.shape.outlineRoughness = 0.0F;
    settings.underside.roughness = 0.0F;
    settings.underside.protrusion = 0.0F;
    for (const IslandShape& shape : shapesOf(settings)) {
        const UndersideMeasures m = measure(shape);
        CHECK(m.outlineRatio < 1.0001);
        CHECK(m.around < 1.0e-4);
        // The profile itself is still not a cone and keeps its ledges.
        CHECK(m.coneDeviation >= 0.08);
        CHECK(m.shelves >= 2.0);
        CHECK(std::abs(outlineRadius(shape, Vec2{0.6F, 0.8F}) - shape.island.radius) <
              1.0e-4F * shape.island.radius);
    }
}

TEST_CASE("Plateaus and terraces follow the settings", "[worldgen][shape]")
{
    WorldGenSettings never = withSeed(9U);
    never.elevation.plateauChance = 0.0F;
    for (const IslandShape& shape : shapesOf(never)) {
        CHECK_FALSE(shape.terraced);
    }

    // Terraces: the heights gather on the levels k/3 of the top height.
    WorldGenSettings terraces = withSeed(9U);
    terraces.elevation.plateauChance = 1.0F;
    terraces.elevation.terraceSteps = 3;
    for (const IslandShape& shape : shapesOf(terraces)) {
        REQUIRE(shape.terraced);
        int onLevel = 0;
        int samples = 0;
        const auto radius = static_cast<double>(shape.island.radius);
        for (int a = -15; a <= 15; ++a) {
            for (int b = -15; b <= 15; ++b) {
                const Vec2 local{static_cast<float>(radius * a / 15.0),
                                 static_cast<float>(radius * b / 15.0)};
                if (outlineDistance(shape, local) >= 0.75F) {
                    continue;
                }
                const double level = static_cast<double>(topHeightAt(shape, local)) /
                                     static_cast<double>(shape.island.topHeight) * 3.0;
                if (std::abs(level - std::round(level)) < 0.01) {
                    ++onLevel;
                }
                ++samples;
            }
        }
        INFO("island " << shape.island.index << ": " << onLevel << " of " << samples);
        CHECK(onLevel * 2 > samples);
    }

    // Plateaus: flat tops at the full height on most islands.
    WorldGenSettings plateaus = terraces;
    plateaus.elevation.terraceSteps = 0;
    int flatTopped = 0;
    const std::vector<IslandShape> shapes = shapesOf(plateaus);
    for (const IslandShape& shape : shapes) {
        bool reachesTop = false;
        const auto radius = static_cast<double>(shape.island.radius);
        for (int a = -15; a <= 15 && !reachesTop; ++a) {
            for (int b = -15; b <= 15; ++b) {
                const Vec2 local{static_cast<float>(radius * a / 15.0),
                                 static_cast<float>(radius * b / 15.0)};
                if (topHeightAt(shape, local) == shape.island.topHeight) {
                    reachesTop = true;
                    break;
                }
            }
        }
        if (reachesTop) {
            ++flatTopped;
        }
    }
    CHECK(flatTopped * 2 > static_cast<int>(shapes.size()));
}

TEST_CASE("Island shapes repeat bit for bit", "[worldgen][shape]")
{
    const WorldGenSettings settings = withSeed(77U);
    const std::vector<IslandShape> first = shapesOf(settings);
    const std::vector<IslandShape> second = shapesOf(settings);
    REQUIRE(first == second);
    int different = 0;
    for (const IslandShape& shape : first) {
        const auto radius = static_cast<double>(shape.island.radius);
        for (int a = -10; a <= 10; ++a) {
            for (int b = -10; b <= 10; ++b) {
                const Vec2 local{static_cast<float>(radius * a / 10.0),
                                 static_cast<float>(radius * b / 10.0)};
                const IslandShape copy = shape;
                if (bitsOf(topHeightAt(shape, local)) != bitsOf(topHeightAt(copy, local)) ||
                    bitsOf(undersideDepthAt(shape, local)) !=
                        bitsOf(undersideDepthAt(copy, local))) {
                    ++different;
                }
            }
        }
    }
    CHECK(different == 0);
}

TEST_CASE("Islands get different shapes", "[worldgen][shape]")
{
    std::set<std::uint32_t> outlines;
    std::set<std::uint32_t> depths;
    std::size_t count = 0;
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        for (const IslandShape& shape : shapesOf(withSeed(seed))) {
            outlines.insert(bitsOf(outlineRadius(shape, Vec2{1.0F, 0.0F}) / shape.island.radius));
            depths.insert(bitsOf(undersideDepthAt(shape, pointAt(shape, Vec2{0.0F, 1.0F}, 0.5)) /
                                 shape.island.undersideDepth));
            ++count;
        }
    }
    CHECK(outlines.size() == count);
    CHECK(depths.size() == count);
}

TEST_CASE("The relief has its own seed", "[worldgen][shape]")
{
    // Changing the relief changes the top, but not the outline or the underside.
    const WorldGenSettings base = withSeed(21U);
    WorldGenSettings other = base;
    other.elevation.ridgeWeight = 0.9F;
    other.noise.frequency = 0.08F;
    const std::vector<IslandShape> a = shapesOf(base);
    const std::vector<IslandShape> b = shapesOf(other);
    REQUIRE(a.size() == b.size());
    int outlineChanged = 0;
    int undersideChanged = 0;
    int topChanged = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (int j = 0; j < 32; ++j) {
            const Vec2 dir = direction(j, 32);
            if (bitsOf(outlineRadius(a[i], dir)) != bitsOf(outlineRadius(b[i], dir))) {
                ++outlineChanged;
            }
            const Vec2 local = pointAt(a[i], dir, 0.4);
            if (bitsOf(undersideDepthAt(a[i], local)) != bitsOf(undersideDepthAt(b[i], local))) {
                ++undersideChanged;
            }
            if (topHeightAt(a[i], local) != topHeightAt(b[i], local)) {
                ++topChanged;
            }
        }
    }
    CHECK(outlineChanged == 0);
    CHECK(undersideChanged == 0);
    CHECK(topChanged > 0);
}

TEST_CASE("Unusual positions and directions are handled", "[worldgen][shape]")
{
    const std::vector<IslandShape> shapes = shapesOf(withSeed(3U));
    const IslandShape& shape = shapes.front();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const Vec2 local : {Vec2{nan, 0.0F}, Vec2{0.0F, infinity}, Vec2{-infinity, nan}}) {
        CHECK(islandMask(shape, local) == 0.0F);
        CHECK(topHeightAt(shape, local) == 0.0F);
        CHECK(undersideDepthAt(shape, local) == 0.0F);
    }
    // A zero or non-finite direction counts as +X; length does not matter.
    const float alongX = outlineRadius(shape, Vec2{1.0F, 0.0F});
    CHECK(outlineRadius(shape, Vec2{0.0F, 0.0F}) == alongX);
    CHECK(outlineRadius(shape, Vec2{nan, 1.0F}) == alongX);
    CHECK(outlineRadius(shape, Vec2{25.0F, 0.0F}) == alongX);
    // The anchor is the deepest point of the underside and inside the mask.
    CHECK(undersideDepthAt(shape, Vec2{0.0F, 0.0F}) == shape.island.undersideDepth);
    CHECK(islandMask(shape, Vec2{0.0F, 0.0F}) == 1.0F);
}
