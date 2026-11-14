// Low-poly clouds around and between the islands, and their drift (M3.10,
// ADR 0017).
#include "engine/worldgen/Clouds.hpp"

#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/IslandMesh.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

using engine::math::Vec3;
using engine::worldgen::CloudCluster;
using engine::worldgen::cloudDrift;
using engine::worldgen::CloudField;
using engine::worldgen::generateClouds;
using engine::worldgen::PlacedIsland;
using engine::worldgen::WorldGenSettings;

namespace {

bool near(float a, float b, float tolerance = 1.0e-4F)
{
    return std::abs(a - b) <= tolerance;
}

bool near(const Vec3& a, const Vec3& b, float tolerance = 1.0e-4F)
{
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

WorldGenSettings withSeed(std::uint64_t seed)
{
    WorldGenSettings settings;
    settings.seed = seed;
    return settings;
}

std::vector<PlacedIsland> layoutOf(const WorldGenSettings& settings)
{
    auto layout = engine::worldgen::placeIslands(settings);
    REQUIRE(layout.hasValue());
    return layout.value().islands;
}

struct Generated {
    WorldGenSettings settings;
    std::vector<PlacedIsland> layout;
    CloudField field;
};

// The clouds of seeds 0 to 15 with the default settings, generated once.
const std::vector<Generated>& corpus()
{
    static const std::vector<Generated> worlds = [] {
        std::vector<Generated> result;
        for (std::uint64_t seed = 0; seed < 16; ++seed) {
            const WorldGenSettings settings = withSeed(seed);
            const std::vector<PlacedIsland> layout = layoutOf(settings);
            result.push_back({settings, layout, generateClouds(settings, layout)});
        }
        return result;
    }();
    return worlds;
}

// The volume rule of ADR 0017: apart horizontally, or apart vertically.
bool keepsClearance(const CloudCluster& cluster, const PlacedIsland& island, float clearance)
{
    const double dx = static_cast<double>(cluster.anchor.x) - static_cast<double>(island.anchor.x);
    const double dz = static_cast<double>(cluster.anchor.z) - static_cast<double>(island.anchor.z);
    const double reach = static_cast<double>(cluster.radius) + static_cast<double>(island.radius) +
                         static_cast<double>(clearance);
    if (dx * dx + dz * dz >= reach * reach) {
        return true;
    }
    const double cloudLow = static_cast<double>(cluster.anchor.y);
    const double cloudHigh = cloudLow + static_cast<double>(cluster.height);
    const double islandLow =
        static_cast<double>(island.anchor.y) - static_cast<double>(island.undersideDepth);
    const double islandHigh =
        static_cast<double>(island.anchor.y) + static_cast<double>(island.topHeight);
    return islandLow - cloudHigh >= static_cast<double>(clearance) ||
           cloudLow - islandHigh >= static_cast<double>(clearance);
}

CloudCluster testCluster(Vec3 anchor, float radius)
{
    CloudCluster cluster;
    cluster.anchor = anchor;
    cluster.radius = radius;
    cluster.height = engine::worldgen::kCloudHeightRatio * radius;
    return cluster;
}

} // namespace

TEST_CASE("Clouds match their golden values", "[worldgen][clouds]")
{
    // Recorded with seed 42 and the default settings; an independent
    // implementation of ADR 0017 gives the same clusters, puffs and mesh
    // hashes (also for seed 1).
    const WorldGenSettings settings = withSeed(42U);
    const CloudField field = generateClouds(settings, layoutOf(settings));
    CHECK(field.requestedCount == 20U);
    REQUIRE(field.clusters.size() == 20U);
    CHECK(field.warning.empty());

    const CloudCluster& first = field.clusters[0];
    CHECK(first.index == 0U);
    CHECK(near(first.anchor, Vec3{74.9755249F, 14.6546955F, -159.812927F}));
    CHECK(near(first.radius, 25.9455757F));
    CHECK(near(first.density, 0.592536986F));
    REQUIRE(first.puffs.size() == 6U);
    CHECK(near(first.puffs.front().radius, 13.9323492F));
    CHECK(near(first.puffs.back().centre, Vec3{-1.92547977F, 3.21159172F, 4.98353243F}));
    CHECK(near(first.puffs.back().radius, 10.7053061F));
    CHECK(first.mesh.triangleCount() == 480U);
    CHECK(engine::worldgen::meshHash(first.mesh) == 0x73A9FC91F83D34E7ULL);

    const CloudCluster& second = field.clusters[1];
    CHECK(near(second.anchor, Vec3{150.131683F, 87.2384491F, 223.627823F}));
    CHECK(near(second.radius, 20.11483F));
    CHECK(near(second.density, 0.686152697F));
    REQUIRE(second.puffs.size() == 6U);
    CHECK(near(second.puffs.front().radius, 9.83034325F));
    CHECK(near(second.puffs.back().centre, Vec3{3.42585945F, 1.80449486F, 3.3037746F}));
    CHECK(engine::worldgen::meshHash(second.mesh) == 0xCB89CB38E35A2532ULL);
}

TEST_CASE("Cloud clusters follow the settings and stay inside the world", "[worldgen][clouds]")
{
    for (const Generated& g : corpus()) {
        INFO("seed " << g.settings.seed);
        const auto& clouds = g.settings.clouds;
        const auto& bounds = g.settings.bounds;
        CHECK(g.field.requestedCount == static_cast<std::size_t>(clouds.clusterCount));
        CHECK(g.field.clusters.size() <= g.field.requestedCount);
        CHECK(g.field.warning.empty() == (g.field.clusters.size() == g.field.requestedCount));
        std::uint64_t previous = 0;
        for (std::size_t i = 0; i < g.field.clusters.size(); ++i) {
            const CloudCluster& cluster = g.field.clusters[i];
            INFO("cluster " << cluster.index);
            CHECK(cluster.index < g.field.requestedCount);
            if (i > 0U) {
                CHECK(cluster.index > previous);
            }
            previous = cluster.index;
            CHECK(cluster.radius >= clouds.size.min);
            CHECK(cluster.radius <= clouds.size.max);
            CHECK(cluster.height == engine::worldgen::kCloudHeightRatio * cluster.radius);
            CHECK(cluster.anchor.y >= clouds.altitude.min);
            CHECK(cluster.anchor.y <= clouds.altitude.max);
            // The whole volume inside the bounds.
            CHECK(cluster.anchor.x - cluster.radius >= bounds.min.x);
            CHECK(cluster.anchor.x + cluster.radius <= bounds.max.x);
            CHECK(cluster.anchor.z - cluster.radius >= bounds.min.z);
            CHECK(cluster.anchor.z + cluster.radius <= bounds.max.z);
            CHECK(cluster.anchor.y + cluster.height <= bounds.max.y);
            // Density and puffs.
            CHECK(cluster.density >= std::max(0.0F, clouds.density - 0.25F));
            CHECK(cluster.density <= std::min(1.0F, clouds.density + 0.25F));
            const auto expected = 1U + static_cast<std::size_t>(std::floor(
                                           static_cast<double>(cluster.density) * 8.0 + 0.5));
            CHECK(cluster.puffs.size() == expected);
            CHECK(cluster.puffs.size() <=
                  static_cast<std::size_t>(engine::worldgen::kMaxCloudPuffs));
            REQUIRE_FALSE(cluster.puffs.empty());
            CHECK(cluster.puffs.front().centre.x == 0.0F);
            CHECK(cluster.puffs.front().centre.z == 0.0F);
            bool largestFirst = true;
            for (const auto& puff : cluster.puffs) {
                largestFirst = largestFirst && puff.radius <= cluster.puffs.front().radius;
                CHECK(puff.centre.y > 0.0F);
            }
            CHECK(largestFirst);
            // Clear of every island.
            for (const PlacedIsland& island : g.layout) {
                INFO("island " << island.index);
                CHECK(keepsClearance(cluster, island, clouds.islandClearance));
            }
        }
    }
}

TEST_CASE("Cloud meshes are valid, opaque and within their cluster", "[worldgen][clouds]")
{
    std::size_t bottoms = 0;
    for (const Generated& g : corpus()) {
        for (const CloudCluster& cluster : g.field.clusters) {
            INFO("seed " << g.settings.seed << " cluster " << cluster.index);
            const auto& mesh = cluster.mesh;
            CHECK_FALSE(engine::geometry::validate(mesh).has_value());
            REQUIRE(mesh.normals.size() == mesh.positions.size());
            // 80 faces per puff, a few may be left out where flattening
            // makes them tiny.
            CHECK(mesh.triangleCount() <= 80U * cluster.puffs.size());
            CHECK(mesh.triangleCount() >= 70U * cluster.puffs.size());
            bool inside = true;
            for (const Vec3& p : mesh.positions) {
                const double reach = static_cast<double>(cluster.radius) * 1.00001;
                inside = inside &&
                         static_cast<double>(p.x) * static_cast<double>(p.x) +
                                 static_cast<double>(p.z) * static_cast<double>(p.z) <=
                             reach * reach &&
                         p.y >= 0.0F && p.y <= cluster.height * 1.00001F;
            }
            CHECK(inside);
            bool opaque = true;
            bool shaded = true;
            bool flatBottom = true;
            for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
                const std::size_t first = mesh.indices[3U * t];
                const Vec3 normal = mesh.normals[first];
                const auto& color = mesh.colors[first];
                opaque = opaque && color.a == 1.0F;
                // White where the face looks up, lavender where it looks down.
                const float up = 0.5F + 0.5F * normal.y;
                shaded = shaded && near(color.r, 0.76F + 0.24F * up, 1.0e-5F) &&
                         near(color.b, 0.92F + 0.08F * up, 1.0e-5F);
                const bool onBase = mesh.positions[first].y == 0.0F &&
                                    mesh.positions[first + 1U].y == 0.0F &&
                                    mesh.positions[first + 2U].y == 0.0F;
                if (onBase) {
                    ++bottoms;
                    flatBottom = flatBottom && near(normal, Vec3{0.0F, -1.0F, 0.0F}, 1.0e-6F);
                }
                const double area = 0.5 * static_cast<double>(glm::length(glm::cross(
                                              mesh.positions[first + 1U] - mesh.positions[first],
                                              mesh.positions[first + 2U] - mesh.positions[first])));
                CHECK(area >= 0.99 * engine::worldgen::kMinCloudTriangleArea);
            }
            CHECK(opaque);
            CHECK(shaded);
            CHECK(flatBottom);
        }
    }
    // The puffs are cut flat at the base.
    CHECK(bottoms > 0U);
}

TEST_CASE("Clouds vary in size, density and altitude", "[worldgen][clouds]")
{
    float smallest = 1.0e9F;
    float largest = 0.0F;
    float lowest = 1.0e9F;
    float highest = -1.0e9F;
    float thinnest = 1.0F;
    float densest = 0.0F;
    std::set<std::size_t> puffCounts;
    std::size_t clusters = 0;
    for (const Generated& g : corpus()) {
        CHECK(g.field.clusters.size() == 20U);
        for (const CloudCluster& cluster : g.field.clusters) {
            ++clusters;
            smallest = std::min(smallest, cluster.radius);
            largest = std::max(largest, cluster.radius);
            lowest = std::min(lowest, cluster.anchor.y);
            highest = std::max(highest, cluster.anchor.y);
            thinnest = std::min(thinnest, cluster.density);
            densest = std::max(densest, cluster.density);
            puffCounts.insert(cluster.puffs.size());
        }
    }
    const auto& clouds = WorldGenSettings{}.clouds;
    CHECK(clusters == 320U);
    CHECK(largest - smallest >= 0.8F * (clouds.size.max - clouds.size.min));
    CHECK(highest - lowest >= 0.8F * (clouds.altitude.max - clouds.altitude.min));
    CHECK(densest - thinnest >= 0.4F);
    CHECK(puffCounts.size() >= 4U);
}

TEST_CASE("Clouds repeat for a seed and depend only on their own settings", "[worldgen][clouds]")
{
    const WorldGenSettings settings = withSeed(7U);
    const std::vector<PlacedIsland> layout = layoutOf(settings);
    const CloudField a = generateClouds(settings, layout);
    const CloudField b = generateClouds(settings, layout);
    REQUIRE(a.clusters.size() == b.clusters.size());
    for (std::size_t i = 0; i < a.clusters.size(); ++i) {
        CHECK(a.clusters[i].anchor == b.clusters[i].anchor);
        CHECK(a.clusters[i].puffs == b.clusters[i].puffs);
        CHECK(a.clusters[i].mesh.positions == b.clusters[i].mesh.positions);
        CHECK(a.clusters[i].mesh.colors == b.clusters[i].mesh.colors);
    }

    // Water, mesh resolution and the wind leave the clouds unchanged.
    WorldGenSettings other = settings;
    other.water.enabled = false;
    other.mesh.resolution = 12;
    other.clouds.windSpeed = 9.0F;
    other.clouds.windDirectionDegrees = 200.0F;
    const CloudField same = generateClouds(other, layoutOf(other));
    REQUIRE(same.clusters.size() == a.clusters.size());
    for (std::size_t i = 0; i < a.clusters.size(); ++i) {
        CHECK(same.clusters[i].anchor == a.clusters[i].anchor);
        CHECK(same.clusters[i].mesh.positions == a.clusters[i].mesh.positions);
    }

    // The density changes the shapes, not the placement.
    WorldGenSettings denser = settings;
    denser.clouds.density = 0.9F;
    const CloudField reshaped = generateClouds(denser, layout);
    REQUIRE(reshaped.clusters.size() == a.clusters.size());
    std::size_t morePuffs = 0;
    for (std::size_t i = 0; i < a.clusters.size(); ++i) {
        CHECK(reshaped.clusters[i].anchor == a.clusters[i].anchor);
        CHECK(reshaped.clusters[i].radius == a.clusters[i].radius);
        if (reshaped.clusters[i].puffs.size() > a.clusters[i].puffs.size()) {
            ++morePuffs;
        }
    }
    CHECK(morePuffs >= a.clusters.size() / 2U);

    // Another seed, other clouds.
    const WorldGenSettings next = withSeed(8U);
    const CloudField different = generateClouds(next, layoutOf(next));
    REQUIRE_FALSE(different.clusters.empty());
    CHECK(different.clusters.front().anchor != a.clusters.front().anchor);
}

TEST_CASE("Cloud settings at their edges", "[worldgen][clouds]")
{
    const WorldGenSettings base = withSeed(3U);
    const std::vector<PlacedIsland> layout = layoutOf(base);

    WorldGenSettings none = base;
    none.clouds.clusterCount = 0;
    const CloudField empty = generateClouds(none, layout);
    CHECK(empty.clusters.empty());
    CHECK(empty.requestedCount == 0U);
    CHECK(empty.warning.empty());

    WorldGenSettings sparse = base;
    sparse.clouds.density = 0.0F;
    for (const CloudCluster& cluster : generateClouds(sparse, layout).clusters) {
        CHECK(cluster.puffs.size() <= 3U);
    }
    WorldGenSettings dense = base;
    dense.clouds.density = 1.0F;
    for (const CloudCluster& cluster : generateClouds(dense, layout).clusters) {
        CHECK(cluster.puffs.size() >= 7U);
    }

    // Clouds only above the islands: all of them fit.
    WorldGenSettings high = base;
    high.clouds.altitude = {140.0F, 150.0F};
    high.clouds.size = {4.0F, 12.0F};
    const CloudField above = generateClouds(high, layout);
    CHECK(above.clusters.size() == above.requestedCount);

    // Large clouds kept far from many islands: some find no room.
    WorldGenSettings crowded = base;
    crowded.islands.count = 40;
    crowded.islands.minHorizontalSpacing = 0.0F;
    crowded.islands.minVerticalSpacing = 0.0F;
    crowded.clouds.clusterCount = 60;
    crowded.clouds.size = {60.0F, 100.0F};
    crowded.clouds.altitude = {-20.0F, 40.0F};
    crowded.clouds.islandClearance = 50.0F;
    const CloudField squeezed = generateClouds(crowded, layoutOf(crowded));
    CHECK(squeezed.clusters.size() < squeezed.requestedCount);
    CHECK(squeezed.warning.starts_with("placed " + std::to_string(squeezed.clusters.size()) +
                                       " of 60 cloud clusters"));
}

TEST_CASE("Clouds drift along the wind and wrap around the world", "[worldgen][clouds]")
{
    WorldGenSettings settings;
    settings.clouds.windDirectionDegrees = 0.0F;
    settings.clouds.windSpeed = 2.0F;
    const CloudCluster cluster = testCluster(Vec3{0.0F, 50.0F, 0.0F}, 10.0F);

    SECTION("at time 0 a cloud is at its anchor at full size")
    {
        for (const Generated& g : corpus()) {
            for (const CloudCluster& c : g.field.clusters) {
                const auto drift = cloudDrift(c, g.settings, 0.0);
                CHECK(drift.position == c.anchor);
                CHECK(drift.scale == 1.0F);
            }
        }
    }
    SECTION("it moves with the wind, keeping its height")
    {
        const auto drift = cloudDrift(cluster, settings, 5.0);
        CHECK(near(drift.position, Vec3{10.0F, 50.0F, 0.0F}));
        CHECK(drift.scale == 1.0F);
        WorldGenSettings north = settings;
        north.clouds.windDirectionDegrees = 90.0F;
        CHECK(near(cloudDrift(cluster, north, 5.0).position, Vec3{0.0F, 50.0F, 10.0F}));
        WorldGenSettings diagonal = settings;
        diagonal.clouds.windDirectionDegrees = 30.0F;
        const Vec3 moved = cloudDrift(cluster, diagonal, 5.0).position;
        CHECK(near(moved.x, 10.0F * std::cos(0.5235988F)));
        CHECK(near(moved.z, 10.0F * std::sin(0.5235988F)));
        WorldGenSettings calm = settings;
        calm.clouds.windSpeed = 0.0F;
        CHECK(cloudDrift(cluster, calm, 1000.0).position == cluster.anchor);
    }
    SECTION("it wraps around in X and Z and shrinks away at the seam")
    {
        // 520 m of world at 2 m/s: back where it started after 260 s.
        CHECK(near(cloudDrift(cluster, settings, 260.0).position, cluster.anchor, 1.0e-3F));
        CHECK(near(cloudDrift(cluster, settings, 1300.0).position, cluster.anchor, 1.0e-3F));
        // At 130 s it reaches the edge (x = 260) and appears at x = -260.
        const auto atSeam = cloudDrift(cluster, settings, 130.0);
        CHECK(near(std::abs(atSeam.position.x), 260.0F, 1.0e-3F));
        CHECK(near(atSeam.scale, 0.0F, 1.0e-5F));
        // One radius before the edge it is still at full size, half a radius
        // before it half its size.
        CHECK(near(cloudDrift(cluster, settings, 125.0).scale, 1.0F, 1.0e-5F));
        CHECK(near(cloudDrift(cluster, settings, 127.5).scale, 0.5F, 1.0e-5F));
        CHECK(near(cloudDrift(cluster, settings, 135.0).position.x, -250.0F, 1.0e-3F));
        WorldGenSettings back = settings;
        back.clouds.windDirectionDegrees = 180.0F;
        CHECK(near(cloudDrift(cluster, back, 135.0).position.x, 250.0F, 1.0e-3F));
    }
    SECTION("it never leaves the bounds")
    {
        WorldGenSettings gusty = settings;
        gusty.clouds.windSpeed = 17.0F;
        gusty.clouds.windDirectionDegrees = 233.0F;
        bool within = true;
        for (int step = 0; step < 2000; ++step) {
            const auto drift = cloudDrift(cluster, gusty, 0.37 * static_cast<double>(step));
            within = within && drift.position.x >= gusty.bounds.min.x &&
                     drift.position.x <= gusty.bounds.max.x &&
                     drift.position.z >= gusty.bounds.min.z &&
                     drift.position.z <= gusty.bounds.max.z && drift.position.y == 50.0F &&
                     drift.scale >= 0.0F && drift.scale <= 1.0F;
        }
        CHECK(within);
    }
}
