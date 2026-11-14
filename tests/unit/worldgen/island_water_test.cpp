// Local water on the islands: lakes, a stream with its ponds, a waterfall
// (M3.9, ADR 0016).
#include "engine/worldgen/IslandWater.hpp"

#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/IslandMesh.hpp"
#include "engine/worldgen/IslandShape.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <utility>
#include <vector>

using engine::math::Vec2;
using engine::math::Vec3;
using engine::worldgen::generateIslandWater;
using engine::worldgen::IslandShape;
using engine::worldgen::IslandWater;
using engine::worldgen::Lake;
using engine::worldgen::makeIslandShape;
using engine::worldgen::outlineDistance;
using engine::worldgen::PlacedIsland;
using engine::worldgen::StreamMouth;
using engine::worldgen::topHeightAt;
using engine::worldgen::WorldGenSettings;

namespace {

bool near(float a, float b)
{
    return std::abs(a - b) <= 1.0e-4F;
}

bool near(const Vec3& a, const Vec3& b)
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

Vec2 xz(const Vec3& p)
{
    return Vec2{p.x, p.z};
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
    IslandShape shape;
    IslandWater water;
};

// The water of every island of seeds 0 to 15 with the default settings,
// generated once.
const std::vector<Generated>& corpus()
{
    static const std::vector<Generated> islands = [] {
        std::vector<Generated> result;
        for (std::uint64_t seed = 0; seed < 16; ++seed) {
            const WorldGenSettings settings = withSeed(seed);
            const std::vector<PlacedIsland> layout = layoutOf(settings);
            for (const PlacedIsland& island : layout) {
                const IslandShape shape = makeIslandShape(island, settings);
                result.push_back(
                    {settings, layout, shape, generateIslandWater(shape, settings, layout)});
            }
        }
        return result;
    }();
    return islands;
}

Vec3 corner(const engine::geometry::MeshData& mesh, std::size_t triangle, std::size_t index)
{
    return mesh.positions[mesh.indices[3U * triangle + index]];
}

Vec3 normalOf(const engine::geometry::MeshData& mesh, std::size_t triangle)
{
    return mesh.normals[mesh.indices[3U * triangle]];
}

// Edges that belong to only one of a range of triangles (the outline of a
// surface), as pairs of corners. Corners are matched by their exact XZ.
std::vector<std::pair<Vec3, Vec3>> boundaryEdges(const engine::geometry::MeshData& mesh,
                                                 std::size_t first, std::size_t count)
{
    using Key = std::pair<std::uint64_t, std::uint64_t>;
    const auto keyOf = [](const Vec3& p) {
        std::uint32_t x = 0;
        std::uint32_t z = 0;
        std::memcpy(&x, &p.x, sizeof(x));
        std::memcpy(&z, &p.z, sizeof(z));
        return (static_cast<std::uint64_t>(x) << 32U) | static_cast<std::uint64_t>(z);
    };
    std::map<Key, std::pair<int, std::pair<Vec3, Vec3>>> edges;
    for (std::size_t t = first; t < first + count; ++t) {
        for (std::size_t e = 0; e < 3U; ++e) {
            const Vec3 a = corner(mesh, t, e);
            const Vec3 b = corner(mesh, t, (e + 1U) % 3U);
            const std::uint64_t ka = keyOf(a);
            const std::uint64_t kb = keyOf(b);
            const Key key{std::min(ka, kb), std::max(ka, kb)};
            auto& entry = edges[key];
            ++entry.first;
            entry.second = {a, b};
        }
    }
    std::vector<std::pair<Vec3, Vec3>> boundary;
    for (const auto& edge : edges) {
        if (edge.second.first == 1) {
            boundary.push_back(edge.second.second);
        }
    }
    return boundary;
}

std::vector<IslandShape> shapesOf(const WorldGenSettings& settings,
                                  const std::vector<PlacedIsland>& layout)
{
    std::vector<IslandShape> shapes;
    for (const PlacedIsland& island : layout) {
        shapes.push_back(makeIslandShape(island, settings));
    }
    return shapes;
}

std::size_t countIf(const std::vector<Lake>& lakes, bool overflows)
{
    return static_cast<std::size_t>(std::count_if(
        lakes.begin(), lakes.end(), [&](const Lake& lake) { return lake.overflows == overflows; }));
}

} // namespace

TEST_CASE("Island water matches its golden values", "[worldgen][water]")
{
    // Recorded with seed 42 and the default settings; an independent
    // implementation of ADR 0016 gives the same values (and the same water
    // for seeds 1 and 7).
    const WorldGenSettings settings = withSeed(42U);
    const std::vector<PlacedIsland> layout = layoutOf(settings);
    REQUIRE(layout.size() == 12U);
    std::vector<IslandWater> water;
    for (const PlacedIsland& island : layout) {
        water.push_back(generateIslandWater(makeIslandShape(island, settings), settings, layout));
    }

    struct Counts {
        std::size_t lakes;
        std::size_t runs;
        std::size_t waterfalls;
    };
    const std::vector<Counts> counts{{0, 1, 1}, {0, 0, 0}, {2, 3, 1}, {2, 3, 0},
                                     {0, 0, 0}, {2, 3, 1}, {0, 1, 1}, {0, 0, 0},
                                     {1, 0, 0}, {1, 0, 0}, {2, 2, 1}, {0, 0, 0}};
    for (std::size_t i = 0; i < counts.size(); ++i) {
        INFO("island " << i);
        CHECK(water[i].lakes.size() == counts[i].lakes);
        CHECK(water[i].streams.size() == counts[i].runs);
        CHECK(water[i].waterfalls.size() == counts[i].waterfalls);
    }

    // Island 2: a stream from its spring through two ponds over the rim, and
    // its waterfall.
    const IslandWater& second = water[2];
    REQUIRE(second.lakes.size() == 2U);
    CHECK(near(second.lakes[0].level, 8.99326515F));
    CHECK(near(second.lakes[0].spillLevel, 8.99326515F));
    CHECK(near(second.lakes[0].bottom, 7.79565334F));
    CHECK(second.lakes[0].overflows);
    CHECK(second.lakes[0].samples == 32U);
    CHECK(near(second.lakes[1].level, 4.83061981F));
    CHECK(near(second.lakes[1].bottom, 4.73213482F));
    CHECK(second.lakes[1].overflows);
    CHECK(second.lakes[1].samples == 1U);
    REQUIRE(second.streams.size() == 3U);
    CHECK(second.streams[0].path.size() == 4U);
    CHECK(near(second.streams[0].path.front(), Vec3{0.0F, 12.5792894F, 16.2913551F}));
    CHECK(near(second.streams[0].path.back(), Vec3{6.10925817F, 8.99326515F, 16.2913551F}));
    CHECK(second.streams[0].mouth == StreamMouth::Lake);
    CHECK(second.streams[0].lake == 0U);
    CHECK(second.streams[1].fromLake);
    CHECK(second.streams[1].sourceLake == 0U);
    CHECK(near(second.streams[1].path.front(), Vec3{26.4734516F, 9.11326504F, 12.2185163F}));
    CHECK(near(second.streams[1].path.back(), Vec3{32.5827103F, 4.83061981F, 8.14567757F}));
    CHECK(second.streams[1].lake == 1U);
    CHECK(second.streams[2].path.size() == 5U);
    CHECK(second.streams[2].mouth == StreamMouth::Rim);
    CHECK(near(second.streams[2].path.back(), Vec3{42.6810722F, 0.119999997F, 8.14567757F}));
    REQUIRE(second.waterfalls.size() == 1U);
    const auto& fall = second.waterfalls[0];
    CHECK(near(fall.top, Vec3{42.8284111F, 0.119999997F, 8.17379761F}));
    CHECK(near(fall.outward.x, 0.982271016F));
    CHECK(near(fall.outward.y, 0.187466308F));
    CHECK(near(fall.width, 4.72751951F));
    CHECK(near(fall.length, 55.4993439F));
    CHECK(near(fall.drift, 3.10193133F));

    // Island 8: one lake filled by chance, below its spill level.
    REQUIRE(water[8].lakes.size() == 1U);
    CHECK(near(water[8].lakes[0].level, 5.50859356F));
    CHECK(near(water[8].lakes[0].spillLevel, 5.63119602F));
    CHECK(near(water[8].lakes[0].bottom, 4.98718977F));
    CHECK_FALSE(water[8].lakes[0].overflows);
    CHECK(water[8].lakes[0].samples == 7U);

    // The meshes, rounded to 1/1024 m (also checked on Linux in CI).
    CHECK(water[0].mesh.triangleCount() == 58U);
    CHECK(engine::worldgen::meshHash(water[0].mesh) == 0xA65A798B38CED1AFULL);
    CHECK(water[2].mesh.triangleCount() == 160U);
    CHECK(engine::worldgen::meshHash(water[2].mesh) == 0x9FB421FB5DD3F48CULL);
    CHECK(water[8].mesh.triangleCount() == 32U);
    CHECK(engine::worldgen::meshHash(water[8].mesh) == 0x575ABDF6C69F4C8AULL);
}

TEST_CASE("Water meshes are valid, translucent and in body order", "[worldgen][water]")
{
    for (const Generated& g : corpus()) {
        INFO("seed " << g.settings.seed << " island " << g.shape.island.index);
        const IslandWater& water = g.water;
        const auto& mesh = water.mesh;
        CHECK_FALSE(engine::geometry::validate(mesh).has_value());
        CHECK(water.empty() == mesh.positions.empty());
        if (!mesh.positions.empty()) {
            CHECK(mesh.normals.size() == mesh.positions.size());
        }
        // Lakes, then stream runs, then waterfalls, without gaps.
        std::size_t next = 0;
        const auto follows = [&](const engine::worldgen::WaterMeshRange& range) {
            const bool ok = range.firstTriangle == next && range.triangleCount > 0U;
            next = range.firstTriangle + range.triangleCount;
            return ok;
        };
        for (const auto& lake : water.lakes) {
            CHECK(follows(lake.mesh));
        }
        for (const auto& stream : water.streams) {
            CHECK(follows(stream.mesh));
        }
        for (const auto& fall : water.waterfalls) {
            CHECK(follows(fall.mesh));
        }
        CHECK(next == mesh.triangleCount());
        bool colours = true;
        for (const auto& color : mesh.colors) {
            colours = colours && color.r >= 0.0F && color.r <= 1.0F && color.g >= 0.0F &&
                      color.g <= 1.0F && color.b >= 0.0F && color.b <= 1.0F && color.a >= 0.0F &&
                      color.a < 1.0F;
        }
        CHECK(colours);
        bool large = true;
        for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
            const Vec3 a = corner(mesh, t, 0);
            const auto area =
                0.5 * static_cast<double>(
                          glm::length(glm::cross(corner(mesh, t, 1) - a, corner(mesh, t, 2) - a)));
            large = large && area >= 0.99 * engine::worldgen::kMinWaterTriangleArea;
        }
        CHECK(large);
    }
}

TEST_CASE("Lakes fill basins of the top and stay on their island", "[worldgen][water]")
{
    for (const Generated& g : corpus()) {
        INFO("seed " << g.settings.seed << " island " << g.shape.island.index);
        const IslandWater& water = g.water;
        CHECK(water.lakes.size() <= static_cast<std::size_t>(g.settings.water.maxLakesPerIsland));
        bool pondsFirst = true;
        bool seenLake = false;
        for (std::size_t l = 0; l < water.lakes.size(); ++l) {
            const Lake& lake = water.lakes[l];
            INFO("lake " << l);
            CHECK(lake.bottom < lake.level);
            CHECK(lake.level <= lake.spillLevel);
            CHECK(lake.overflows == (lake.level == lake.spillLevel));
            CHECK(lake.samples >= (lake.overflows ? 1U : 3U));
            CHECK(topHeightAt(g.shape, lake.deepest) == lake.bottom);
            if (lake.overflows && seenLake) {
                pondsFirst = false;
            }
            seenLake = seenLake || !lake.overflows;

            // A flat, upward surface inside the footprint.
            bool flat = true;
            bool inside = true;
            bool underWater = false;
            for (std::size_t t = lake.mesh.firstTriangle;
                 t < lake.mesh.firstTriangle + lake.mesh.triangleCount; ++t) {
                flat = flat && normalOf(water.mesh, t) == Vec3{0.0F, 1.0F, 0.0F};
                for (std::size_t c = 0; c < 3U; ++c) {
                    const Vec3 p = corner(water.mesh, t, c);
                    flat = flat && p.y == lake.level;
                    inside = inside && outlineDistance(g.shape, xz(p)) < 1.0F;
                    underWater = underWater || topHeightAt(g.shape, xz(p)) < lake.level;
                }
            }
            CHECK(flat);
            CHECK(inside);
            CHECK(underWater);
            // It fills a basin: the terrain along its outline is at or above
            // its surface.
            bool enclosed = true;
            for (const auto& edge :
                 boundaryEdges(water.mesh, lake.mesh.firstTriangle, lake.mesh.triangleCount)) {
                enclosed = enclosed && topHeightAt(g.shape, xz(edge.first)) >= lake.level &&
                           topHeightAt(g.shape, xz(edge.second)) >= lake.level;
            }
            CHECK(enclosed);
        }
        CHECK(pondsFirst);
    }
}

TEST_CASE("Streams run downhill from a spring through their ponds to the rim", "[worldgen][water]")
{
    for (const Generated& g : corpus()) {
        INFO("seed " << g.settings.seed << " island " << g.shape.island.index);
        const IslandWater& water = g.water;
        std::size_t pondsReached = 0;
        for (std::size_t r = 0; r < water.streams.size(); ++r) {
            const auto& run = water.streams[r];
            INFO("run " << r);
            REQUIRE(run.path.size() >= 2U);
            CHECK(run.width == water.streams.front().width);
            CHECK(run.width > 0.0F);
            // Chained: spring, pond, ..., rim.
            if (r == 0U) {
                CHECK_FALSE(run.fromLake);
                const Vec3& spring = run.path.front();
                CHECK(outlineDistance(g.shape, xz(spring)) <=
                      engine::worldgen::kStreamSourceMaxDistance);
                CHECK(spring.y >= 1.0F + engine::worldgen::kStreamLift);
            } else {
                CHECK(run.fromLake);
                CHECK(run.sourceLake == water.streams[r - 1U].lake);
            }
            const bool last = r + 1U == water.streams.size();
            CHECK((run.mouth == StreamMouth::Rim) == last);

            bool downhill = true;
            bool inside = true;
            bool onTerrain = true;
            for (std::size_t i = 0; i < run.path.size(); ++i) {
                const Vec3& p = run.path[i];
                if (i > 0U) {
                    downhill = downhill && p.y <= run.path[i - 1U].y;
                }
                inside = inside && outlineDistance(g.shape, xz(p)) < 1.0F;
                if (i + 1U < run.path.size()) {
                    onTerrain = onTerrain &&
                                p.y == topHeightAt(g.shape, xz(p)) + engine::worldgen::kStreamLift;
                }
            }
            CHECK(downhill);
            CHECK(inside);
            CHECK(onTerrain);
            const Vec3& mouth = run.path.back();
            if (run.mouth == StreamMouth::Rim) {
                CHECK(outlineDistance(g.shape, xz(mouth)) >= 0.999F);
                CHECK(mouth.y == engine::worldgen::kStreamLift);
            } else {
                REQUIRE(run.lake < water.lakes.size());
                CHECK(run.lake == pondsReached);
                CHECK(water.lakes[run.lake].overflows);
                CHECK(mouth.y == water.lakes[run.lake].level);
                ++pondsReached;
            }
            // The ribbon faces up.
            bool up = true;
            for (std::size_t t = run.mesh.firstTriangle;
                 t < run.mesh.firstTriangle + run.mesh.triangleCount; ++t) {
                up = up && normalOf(water.mesh, t).y > 0.0F;
            }
            CHECK(up);
            CHECK(run.mesh.triangleCount <= 2U * (run.path.size() - 1U));
        }
        // Every pond lies on the stream.
        CHECK(countIf(water.lakes, true) == pondsReached);
    }
}

TEST_CASE("Waterfalls start at the rim and fall clear of the other islands", "[worldgen][water]")
{
    for (const Generated& g : corpus()) {
        INFO("seed " << g.settings.seed << " island " << g.shape.island.index);
        const IslandWater& water = g.water;
        REQUIRE(water.waterfalls.size() <= 1U);
        if (water.waterfalls.empty()) {
            continue;
        }
        const auto& fall = water.waterfalls.front();
        REQUIRE_FALSE(water.streams.empty());
        const Vec3& mouth = water.streams.back().path.back();
        CHECK(water.streams.back().mouth == StreamMouth::Rim);
        // Its top edge: just outside the rim where the stream leaves.
        CHECK(near(glm::length(fall.outward), 1.0F));
        CHECK(near(glm::length(glm::normalize(xz(mouth)) - fall.outward), 0.0F));
        CHECK(near(fall.top, mouth + Vec3{fall.outward.x, 0.0F, fall.outward.y} *
                                         engine::worldgen::kWaterfallMargin));
        CHECK(outlineDistance(g.shape, xz(fall.top)) >= 1.0F);
        CHECK(fall.width > water.streams.back().width);
        CHECK(fall.length >= 0.8F * 0.9999F * g.shape.island.undersideDepth);
        CHECK(fall.length <= 1.4F * 1.0001F * g.shape.island.undersideDepth);
        CHECK(fall.drift > 0.0F);

        // Both sides of a ribbon from its top edge down the length of the
        // fall, facing away from and towards the island.
        int away = 0;
        int towards = 0;
        bool within = true;
        for (std::size_t t = fall.mesh.firstTriangle;
             t < fall.mesh.firstTriangle + fall.mesh.triangleCount; ++t) {
            const Vec3 n = normalOf(water.mesh, t);
            if (n.x * fall.outward.x + n.z * fall.outward.y > 0.0F) {
                ++away;
            } else {
                ++towards;
            }
            for (std::size_t c = 0; c < 3U; ++c) {
                const Vec3 p = corner(water.mesh, t, c);
                within = within && p.y <= fall.top.y + 1.0e-4F &&
                         p.y >= fall.top.y - fall.length - 1.0e-4F;
            }
        }
        CHECK(away == towards);
        CHECK(away > 0);
        CHECK(within);

        // No part of it enters another island's volume.
        const Vec3 anchor = g.shape.island.anchor;
        bool clear = true;
        for (const PlacedIsland& other : g.layout) {
            if (other.index == g.shape.island.index) {
                continue;
            }
            for (std::size_t t = fall.mesh.firstTriangle;
                 t < fall.mesh.firstTriangle + fall.mesh.triangleCount; ++t) {
                for (std::size_t c = 0; c < 3U; ++c) {
                    const Vec3 p = anchor + corner(water.mesh, t, c);
                    const bool level = p.y >= other.anchor.y - other.undersideDepth &&
                                       p.y <= other.anchor.y + other.topHeight;
                    clear =
                        clear && !(level && glm::length(xz(p) - xz(other.anchor)) < other.radius);
                }
            }
        }
        CHECK(clear);
    }
}

TEST_CASE("A seed corpus has local water on some islands and none on others", "[worldgen][water]")
{
    std::size_t islands = 0;
    std::size_t withLake = 0;
    std::size_t withPond = 0;
    std::size_t withStream = 0;
    std::size_t withWaterfall = 0;
    std::size_t dry = 0;
    for (const Generated& g : corpus()) {
        ++islands;
        const IslandWater& water = g.water;
        withLake += countIf(water.lakes, false) > 0U ? 1U : 0U;
        withPond += countIf(water.lakes, true) > 0U ? 1U : 0U;
        withStream += water.streams.empty() ? 0U : 1U;
        withWaterfall += water.waterfalls.empty() ? 0U : 1U;
        dry += water.empty() ? 1U : 0U;
    }
    INFO(islands << " islands: " << withLake << " with lakes, " << withPond << " with ponds, "
                 << withStream << " with a stream, " << withWaterfall << " with a waterfall, "
                 << dry << " dry");
    CHECK(islands == 192U);
    CHECK(withLake >= islands / 5U);
    CHECK(withPond >= islands / 20U);
    CHECK(withStream >= islands / 5U);
    CHECK(withWaterfall >= islands / 10U);
    CHECK(dry >= islands / 10U);
    CHECK(dry <= islands / 2U);
}

TEST_CASE("Island water is deterministic and differs between seeds", "[worldgen][water]")
{
    const WorldGenSettings settings = withSeed(5U);
    const std::vector<PlacedIsland> layout = layoutOf(settings);
    std::size_t differing = 0;
    for (const PlacedIsland& island : layout) {
        const IslandShape shape = makeIslandShape(island, settings);
        const IslandWater a = generateIslandWater(shape, settings, layout);
        const IslandWater b = generateIslandWater(shape, settings, layout);
        CHECK(a.lakes == b.lakes);
        CHECK(a.streams == b.streams);
        CHECK(a.waterfalls == b.waterfalls);
        CHECK(a.mesh.positions == b.mesh.positions);
        CHECK(a.mesh.colors == b.mesh.colors);
        CHECK(a.mesh.indices == b.mesh.indices);

        // The same island with the water of another world seed.
        WorldGenSettings other = settings;
        other.seed = 6U;
        const IslandWater c = generateIslandWater(shape, other, layout);
        if (engine::worldgen::meshHash(a.mesh) != engine::worldgen::meshHash(c.mesh)) {
            ++differing;
        }
    }
    CHECK(differing >= layout.size() / 3U);
}

TEST_CASE("Water settings switch each kind of water body", "[worldgen][water]")
{
    const WorldGenSettings settings = withSeed(3U);
    const std::vector<PlacedIsland> layout = layoutOf(settings);
    const std::vector<IslandShape> shapes = shapesOf(settings, layout);
    const auto waterOf = [&](const WorldGenSettings& s, std::span<const PlacedIsland> obstacles) {
        std::vector<IslandWater> result;
        for (const IslandShape& shape : shapes) {
            result.push_back(generateIslandWater(shape, s, obstacles));
        }
        return result;
    };
    const std::vector<IslandWater> defaults = waterOf(settings, layout);

    SECTION("disabled: no water at all")
    {
        WorldGenSettings off = settings;
        off.water.enabled = false;
        for (const IslandWater& water : waterOf(off, layout)) {
            CHECK(water.empty());
            CHECK(water.mesh.positions.empty());
        }
    }
    SECTION("no streams: no ponds and no waterfalls either")
    {
        WorldGenSettings noStreams = settings;
        noStreams.water.streamChance = 0.0F;
        for (const IslandWater& water : waterOf(noStreams, layout)) {
            CHECK(water.streams.empty());
            CHECK(water.waterfalls.empty());
            CHECK(countIf(water.lakes, true) == 0U);
        }
    }
    SECTION("no lake chance: only the stream's ponds; streams unchanged")
    {
        WorldGenSettings noLakes = settings;
        noLakes.water.lakeChance = 0.0F;
        const std::vector<IslandWater> water = waterOf(noLakes, layout);
        for (std::size_t i = 0; i < water.size(); ++i) {
            CHECK(countIf(water[i].lakes, false) == 0U);
            REQUIRE(water[i].streams.size() == defaults[i].streams.size());
            for (std::size_t r = 0; r < water[i].streams.size(); ++r) {
                CHECK(water[i].streams[r].path == defaults[i].streams[r].path);
            }
            CHECK(water[i].waterfalls.size() == defaults[i].waterfalls.size());
        }
    }
    SECTION("no lakes allowed: no ponds, every stream is one run")
    {
        WorldGenSettings none = settings;
        none.water.maxLakesPerIsland = 0;
        for (const IslandWater& water : waterOf(none, layout)) {
            CHECK(water.lakes.empty());
            CHECK(water.streams.size() <= 1U);
        }
    }
    SECTION("waterfall chance: none at 0, one per stream at 1 without obstacles; lakes and "
            "streams unchanged")
    {
        WorldGenSettings never = settings;
        never.water.waterfallChance = 0.0F;
        const std::vector<IslandWater> without = waterOf(never, layout);
        WorldGenSettings always = settings;
        always.water.waterfallChance = 1.0F;
        const std::vector<IslandWater> with = waterOf(always, {});
        std::size_t streams = 0;
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            CHECK(without[i].waterfalls.empty());
            CHECK(without[i].lakes.size() == defaults[i].lakes.size());
            CHECK(without[i].streams.size() == defaults[i].streams.size());
            CHECK(with[i].waterfalls.size() == (with[i].streams.empty() ? 0U : 1U));
            streams += with[i].streams.empty() ? 0U : 1U;
        }
        CHECK(streams > 0U);
    }
    SECTION("every chance at its maximum")
    {
        WorldGenSettings all = settings;
        all.water.lakeChance = 1.0F;
        all.water.streamChance = 1.0F;
        all.water.maxLakesPerIsland = 4;
        std::size_t lakes = 0;
        for (const IslandWater& water : waterOf(all, layout)) {
            CHECK(water.lakes.size() <= 4U);
            lakes += water.lakes.size();
        }
        std::size_t defaultLakes = 0;
        for (const IslandWater& water : defaults) {
            defaultLakes += water.lakes.size();
        }
        CHECK(lakes > defaultLakes);
    }
}

TEST_CASE("Islands without suitable relief get no water", "[worldgen][water]")
{
    WorldGenSettings settings = withSeed(3U);
    settings.water.lakeChance = 1.0F;
    settings.water.streamChance = 1.0F;
    settings.water.waterfallChance = 1.0F;
    settings.water.maxLakesPerIsland = 4;
    const std::vector<PlacedIsland> layout = layoutOf(settings);

    std::size_t streams = 0;
    for (const PlacedIsland& island : layout) {
        INFO("island " << island.index);
        // A flat top at rim height: no basin, no spring above the rim.
        IslandShape flat = makeIslandShape(island, settings);
        flat.island.topHeight = 0.0F;
        CHECK(generateIslandWater(flat, settings).empty());

        // A round cone: the top falls from the centre to the rim everywhere,
        // so there is no basin for a lake or a pond; a stream runs to the rim
        // in one run (if a drawn spring lies at least 1 m above the rim).
        IslandShape cone = makeIslandShape(island, settings);
        cone.outlineVariation = 0.0F;
        cone.shortRadius = cone.longRadius;
        cone.relief.frequency = 0.0F;
        cone.terraced = false;
        const IslandWater water = generateIslandWater(cone, settings);
        CHECK(water.lakes.empty());
        CHECK(water.streams.size() <= 1U);
        CHECK(water.waterfalls.size() == water.streams.size());
        streams += water.streams.size();
    }
    CHECK(streams >= layout.size() / 2U);
}
