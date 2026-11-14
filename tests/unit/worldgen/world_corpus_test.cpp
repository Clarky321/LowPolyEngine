// The exit criterion of M3 over a seed corpus (M3.13): whole archipelagos for
// 16 seeds with the default settings are valid in every part, repeat for a
// seed (hashes) and differ between seeds.
#include "engine/worldgen/Archipelago.hpp"

#include "engine/worldgen/IslandMesh.hpp"
#include "engine/worldgen/Random.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

using engine::worldgen::Archipelago;
using engine::worldgen::meshHash;
using engine::worldgen::PlacedIsland;
using engine::worldgen::WorldGenSettings;

namespace {

constexpr std::uint64_t kCorpusSeeds = 16;

Archipelago generate(std::uint64_t seed)
{
    WorldGenSettings settings;
    settings.seed = seed;
    auto archipelago = engine::worldgen::generateArchipelago(settings);
    REQUIRE(archipelago.hasValue());
    return std::move(archipelago).value();
}

// The worlds of seeds 0 to 15, generated once.
const std::vector<Archipelago>& corpus()
{
    static const std::vector<Archipelago> worlds = [] {
        std::vector<Archipelago> result;
        for (std::uint64_t seed = 0; seed < kCorpusSeeds; ++seed) {
            result.push_back(generate(seed));
        }
        return result;
    }();
    return worlds;
}

std::uint64_t fold(std::uint64_t hash, std::uint64_t value)
{
    return engine::worldgen::mix64((hash ^ value) + engine::worldgen::kSplitMix64Gamma);
}

// A coordinate rounded to 1/1024 m, like meshHash, so the fingerprint does not
// depend on differences far below a millimetre between platforms.
std::uint64_t quantized(float value)
{
    return static_cast<std::uint64_t>(std::llround(static_cast<double>(value) * 1024.0));
}

// One hash of a whole world: every island's placement, palette, mesh and
// water mesh, and every cloud cluster's placement and mesh, in order.
std::uint64_t fingerprint(const Archipelago& archipelago)
{
    std::uint64_t hash = fold(0U, archipelago.islands.size());
    for (const auto& island : archipelago.islands) {
        const PlacedIsland& placed = island.shape.island;
        hash = fold(hash, placed.index);
        hash = fold(hash, quantized(placed.anchor.x));
        hash = fold(hash, quantized(placed.anchor.y));
        hash = fold(hash, quantized(placed.anchor.z));
        hash = fold(hash, quantized(placed.radius));
        hash = fold(hash, island.palette.basePalette);
        hash = fold(hash, meshHash(island.mesh));
        hash = fold(hash, meshHash(island.water.mesh));
    }
    hash = fold(hash, archipelago.clouds.clusters.size());
    for (const auto& cluster : archipelago.clouds.clusters) {
        hash = fold(hash, cluster.index);
        hash = fold(hash, quantized(cluster.anchor.x));
        hash = fold(hash, quantized(cluster.anchor.y));
        hash = fold(hash, quantized(cluster.anchor.z));
        hash = fold(hash, meshHash(cluster.mesh));
    }
    return hash;
}

// The spacing rule of the placement: apart horizontally, or apart vertically.
bool keepApart(const PlacedIsland& a, const PlacedIsland& b, const WorldGenSettings& settings)
{
    const double dx = static_cast<double>(a.anchor.x) - static_cast<double>(b.anchor.x);
    const double dz = static_cast<double>(a.anchor.z) - static_cast<double>(b.anchor.z);
    const double reach = static_cast<double>(a.radius) + static_cast<double>(b.radius) +
                         static_cast<double>(settings.islands.minHorizontalSpacing);
    if (dx * dx + dz * dz >= reach * reach * (1.0 - 1.0e-6)) {
        return true;
    }
    const double aLow = static_cast<double>(a.anchor.y) - static_cast<double>(a.undersideDepth);
    const double aHigh = static_cast<double>(a.anchor.y) + static_cast<double>(a.topHeight);
    const double bLow = static_cast<double>(b.anchor.y) - static_cast<double>(b.undersideDepth);
    const double bHigh = static_cast<double>(b.anchor.y) + static_cast<double>(b.topHeight);
    return std::max(bLow - aHigh, aLow - bHigh) >=
           static_cast<double>(settings.islands.minVerticalSpacing) - 1.0e-3;
}

} // namespace

TEST_CASE("Every world of the seed corpus is valid in every part", "[worldgen][corpus]")
{
    const WorldGenSettings settings;
    const auto& bounds = settings.bounds;
    constexpr float kTolerance = 1.0e-3F;
    const auto n = static_cast<std::size_t>(settings.mesh.resolution);
    for (std::uint64_t seed = 0; seed < kCorpusSeeds; ++seed) {
        const Archipelago& world = corpus()[seed];
        INFO("seed " << seed);
        CHECK(world.layout.requestedCount == settings.islands.count);
        REQUIRE(world.islands.size() == world.layout.islands.size());
        CHECK(world.layout.warning.empty() ==
              (world.islands.size() == static_cast<std::size_t>(settings.islands.count)));
        CHECK(world.islands.size() >= 10U);

        for (std::size_t i = 0; i < world.islands.size(); ++i) {
            const auto& island = world.islands[i];
            const PlacedIsland& placed = island.shape.island;
            INFO("island " << placed.index);
            // Placement: inside the bounds, apart from every other island.
            CHECK(placed.anchor.x - placed.radius >= bounds.min.x - kTolerance);
            CHECK(placed.anchor.x + placed.radius <= bounds.max.x + kTolerance);
            CHECK(placed.anchor.z - placed.radius >= bounds.min.z - kTolerance);
            CHECK(placed.anchor.z + placed.radius <= bounds.max.z + kTolerance);
            CHECK(placed.anchor.y - placed.undersideDepth >= bounds.min.y - kTolerance);
            CHECK(placed.anchor.y + placed.topHeight <= bounds.max.y + kTolerance);
            for (std::size_t j = i + 1U; j < world.islands.size(); ++j) {
                CHECK(keepApart(placed, world.islands[j].shape.island, settings));
            }
            // Geometry: a valid, closed-size mesh inside the island's volume.
            CHECK_FALSE(engine::geometry::validate(island.mesh).has_value());
            CHECK(island.mesh.triangleCount() == 2U * n * n + 4U * n * (n - 1U));
            bool inVolume = true;
            for (const auto& p : island.mesh.positions) {
                const double horizontal =
                    std::sqrt(static_cast<double>(p.x) * static_cast<double>(p.x) +
                              static_cast<double>(p.z) * static_cast<double>(p.z));
                inVolume = inVolume &&
                           horizontal <= static_cast<double>(placed.radius) * 1.000001 &&
                           p.y <= placed.topHeight + kTolerance &&
                           p.y >= -placed.undersideDepth - kTolerance;
            }
            CHECK(inVolume);
            // Water: valid, and its lakes and streams on the island.
            CHECK_FALSE(engine::geometry::validate(island.water.mesh).has_value());
            bool onIsland = true;
            for (const auto& lake : island.water.lakes) {
                onIsland = onIsland && lake.level <= placed.topHeight;
            }
            for (const auto& stream : island.water.streams) {
                for (const auto& p : stream.path) {
                    const double horizontal =
                        std::sqrt(static_cast<double>(p.x) * static_cast<double>(p.x) +
                                  static_cast<double>(p.z) * static_cast<double>(p.z));
                    onIsland = onIsland && horizontal <= static_cast<double>(placed.radius);
                }
            }
            CHECK(onIsland);
        }

        // Clouds: valid meshes inside the bounds, clear of every island.
        for (const auto& cluster : world.clouds.clusters) {
            INFO("cloud " << cluster.index);
            CHECK_FALSE(engine::geometry::validate(cluster.mesh).has_value());
            CHECK(cluster.anchor.x - cluster.radius >= bounds.min.x - kTolerance);
            CHECK(cluster.anchor.x + cluster.radius <= bounds.max.x + kTolerance);
            CHECK(cluster.anchor.z - cluster.radius >= bounds.min.z - kTolerance);
            CHECK(cluster.anchor.z + cluster.radius <= bounds.max.z + kTolerance);
            CHECK(cluster.anchor.y + cluster.height <= bounds.max.y + kTolerance);
            for (const auto& island : world.islands) {
                const PlacedIsland& placed = island.shape.island;
                PlacedIsland cloudVolume;
                cloudVolume.anchor = cluster.anchor;
                cloudVolume.radius = cluster.radius;
                cloudVolume.topHeight = cluster.height;
                cloudVolume.undersideDepth = 0.0F;
                WorldGenSettings clearance;
                clearance.islands.minHorizontalSpacing = settings.clouds.islandClearance;
                clearance.islands.minVerticalSpacing = settings.clouds.islandClearance;
                CHECK(keepApart(cloudVolume, placed, clearance));
            }
        }
    }
}

TEST_CASE("Seeds give measurably different worlds", "[worldgen][corpus]")
{
    std::set<std::uint64_t> fingerprints;
    std::set<std::uint64_t> triangles;
    std::set<std::size_t> palettes;
    std::set<std::size_t> lakes;
    std::set<long long> meanAltitudes;
    for (const Archipelago& world : corpus()) {
        fingerprints.insert(fingerprint(world));
        triangles.insert(world.triangleCount());
        lakes.insert(world.waterCounts().lakes);
        double altitude = 0.0;
        for (const auto& island : world.islands) {
            palettes.insert(island.palette.basePalette);
            altitude += static_cast<double>(island.shape.island.anchor.y);
        }
        altitude /= static_cast<double>(world.islands.size());
        meanAltitudes.insert(std::llround(altitude));
    }
    // Every seed its own world.
    CHECK(fingerprints.size() == kCorpusSeeds);
    CHECK(triangles.size() >= kCorpusSeeds - 2U);
    CHECK(lakes.size() >= 5U);
    CHECK(meanAltitudes.size() >= 8U);
    // All six palettes appear across the corpus.
    CHECK(palettes.size() == 6U);
}

TEST_CASE("A seed repeats its world", "[worldgen][corpus]")
{
    for (const std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{7}, std::uint64_t{15}}) {
        INFO("seed " << seed);
        const Archipelago again = generate(seed);
        CHECK(fingerprint(again) == fingerprint(corpus()[seed]));
        CHECK(again.triangleCount() == corpus()[seed].triangleCount());
        CHECK(again.layout == corpus()[seed].layout);
    }
    // Recorded on Windows; CI checks Linux against the same values.
    CHECK(fingerprint(corpus()[1]) == 0x557C88283BDF8EFCULL);
    CHECK(fingerprint(generate(42U)) == 0x1123E3ABAC3ED53DULL);
}
