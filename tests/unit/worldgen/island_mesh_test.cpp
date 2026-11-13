// Island meshes: top grid, underside rings, closure and hashes (ADR 0014,
// Amendment 1).
#include "engine/worldgen/IslandMesh.hpp"

#include "engine/geometry/MeshData.hpp"
#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/IslandShape.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>
#include <vector>

using engine::geometry::MeshData;
using engine::math::Vec3;
using engine::math::Vec4;
using engine::worldgen::buildIslandMesh;
using engine::worldgen::IslandMesh;
using engine::worldgen::IslandShape;
using engine::worldgen::kMinIslandTriangleArea;
using engine::worldgen::makeIslandShape;
using engine::worldgen::meshHash;
using engine::worldgen::outlineDistance;
using engine::worldgen::PlacedIsland;
using engine::worldgen::placeIslands;
using engine::worldgen::WorldGenSettings;

namespace {

WorldGenSettings withSeed(std::uint64_t seed)
{
    WorldGenSettings settings;
    settings.seed = seed;
    return settings;
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

struct BuiltIsland {
    std::uint64_t seed;
    IslandShape shape;
    IslandMesh mesh;
};

// The islands of seeds 0 to 5 with the default settings (72 islands), built
// once and shared by the tests below.
const std::vector<BuiltIsland>& corpus()
{
    static const std::vector<BuiltIsland> built = [] {
        std::vector<BuiltIsland> islands;
        for (std::uint64_t seed = 0; seed < 6; ++seed) {
            const WorldGenSettings settings = withSeed(seed);
            for (const IslandShape& shape : shapesOf(settings)) {
                islands.push_back(
                    BuiltIsland{seed, shape, buildIslandMesh(shape, settings.mesh.resolution)});
            }
        }
        return islands;
    }();
    return built;
}

using PositionKey = std::array<std::uint32_t, 3>;

PositionKey keyOf(const Vec3& position)
{
    return {std::bit_cast<std::uint32_t>(position.x), std::bit_cast<std::uint32_t>(position.y),
            std::bit_cast<std::uint32_t>(position.z)};
}

double triangleArea(const MeshData& mesh, std::size_t triangle)
{
    const Vec3& a = mesh.positions[mesh.indices[3U * triangle]];
    const Vec3& b = mesh.positions[mesh.indices[3U * triangle + 1U]];
    const Vec3& c = mesh.positions[mesh.indices[3U * triangle + 2U]];
    const glm::dvec3 ab{static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y),
                        static_cast<double>(b.z - a.z)};
    const glm::dvec3 ac{static_cast<double>(c.x - a.x), static_cast<double>(c.y - a.y),
                        static_cast<double>(c.z - a.z)};
    return 0.5 * glm::length(glm::cross(ab, ac));
}

double smallestTriangle(const IslandMesh& mesh)
{
    double smallest = std::numeric_limits<double>::max();
    for (const MeshData* part : {&mesh.top, &mesh.underside}) {
        for (std::size_t t = 0; t < part->triangleCount(); ++t) {
            smallest = std::min(smallest, triangleArea(*part, t));
        }
    }
    return smallest;
}

// Closure of the two parts together, on exact positions: every directed edge
// appears once and its reverse once, and the surface is a sphere (V - E + F = 2).
struct Closure {
    std::size_t unmatchedEdges = 0;
    std::size_t repeatedEdges = 0;
    long long eulerCharacteristic = 0;
};

Closure closureOf(const IslandMesh& mesh)
{
    // Weld the corners of all triangles by exact position: corner c of the
    // combined triangle list gets the number of its distinct position.
    std::vector<std::pair<PositionKey, std::uint32_t>> corners;
    for (const MeshData* part : {&mesh.top, &mesh.underside}) {
        for (const std::uint32_t index : part->indices) {
            corners.emplace_back(keyOf(part->positions[index]),
                                 static_cast<std::uint32_t>(corners.size()));
        }
    }
    std::sort(corners.begin(), corners.end());
    std::vector<std::uint32_t> welded(corners.size());
    std::uint32_t distinct = 0;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        if (i > 0U && corners[i].first != corners[i - 1U].first) {
            ++distinct;
        }
        welded[corners[i].second] = distinct;
    }
    const std::size_t vertexCount = corners.empty() ? 0U : distinct + 1U;

    // Directed edges as (from << 32 | to), sorted for lookups.
    const std::size_t faces = welded.size() / 3U;
    std::vector<std::uint64_t> directed;
    directed.reserve(welded.size());
    for (std::size_t t = 0; t < faces; ++t) {
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const std::uint64_t from = welded[3U * t + corner];
            const std::uint64_t to = welded[3U * t + (corner + 1U) % 3U];
            directed.push_back((from << 32U) | to);
        }
    }
    std::sort(directed.begin(), directed.end());

    Closure closure;
    for (std::size_t i = 0; i + 1U < directed.size(); ++i) {
        if (directed[i] == directed[i + 1U]) {
            ++closure.repeatedEdges;
        }
    }
    for (const std::uint64_t edge : directed) {
        const std::uint64_t reverse = (edge << 32U) | (edge >> 32U);
        if (!std::binary_search(directed.begin(), directed.end(), reverse)) {
            ++closure.unmatchedEdges;
        }
    }
    const auto edges = static_cast<long long>(directed.size() / 2U);
    closure.eulerCharacteristic =
        static_cast<long long>(vertexCount) - edges + static_cast<long long>(faces);
    return closure;
}

} // namespace

TEST_CASE("Island meshes are valid and flat-shaded", "[worldgen][islandmesh]")
{
    for (const BuiltIsland& built : corpus()) {
        INFO("seed " << built.seed << " island " << built.shape.island.index);
        for (const MeshData* part : {&built.mesh.top, &built.mesh.underside}) {
            CHECK_FALSE(engine::geometry::validate(*part).has_value());
            CHECK(part->hasNormals());
            CHECK_FALSE(part->hasUvs());
            // Own vertices per triangle, numbered in order, white, one normal per face.
            REQUIRE(part->vertexCount() == 3U * part->triangleCount());
            bool sequential = true;
            bool white = true;
            bool flat = true;
            for (std::size_t i = 0; i < part->indices.size(); ++i) {
                sequential = sequential && part->indices[i] == i;
                white = white && part->colors[i] == Vec4{1.0F, 1.0F, 1.0F, 1.0F};
                flat = flat && part->normals[i] == part->normals[i - i % 3U];
            }
            CHECK(sequential);
            CHECK(white);
            CHECK(flat);
        }
    }
}

TEST_CASE("Triangle counts follow the mesh resolution", "[worldgen][islandmesh]")
{
    const IslandShape& shape = corpus().front().shape;
    for (const int n : {8, 9, 48, 101}) {
        INFO("resolution " << n);
        const IslandMesh mesh = buildIslandMesh(shape, n);
        const auto cells = static_cast<std::size_t>(n);
        const std::size_t rings = (cells + 1U) / 2U;
        CHECK(mesh.top.triangleCount() == 2U * cells * cells);
        CHECK(mesh.underside.triangleCount() == 4U * cells * (2U * rings - 1U));
        CHECK(mesh.top.triangleCount() + mesh.underside.triangleCount() <= 6U * cells * cells);
    }
    // Out-of-range resolutions are clamped to 8 and 256.
    CHECK(buildIslandMesh(shape, 2).top.triangleCount() == 2U * 8U * 8U);
    CHECK(buildIslandMesh(shape, 1000).top.triangleCount() == 2U * 256U * 256U);
}

TEST_CASE("The top faces up and the underside faces down", "[worldgen][islandmesh]")
{
    // Both parts are height fields over the footprint, so counter-clockwise seen
    // from outside means a normal pointing up on the top and down underneath.
    int wrongTop = 0;
    int wrongUnderside = 0;
    for (const BuiltIsland& built : corpus()) {
        for (const Vec3& normal : built.mesh.top.normals) {
            wrongTop += normal.y > 0.0F ? 0 : 1;
        }
        for (const Vec3& normal : built.mesh.underside.normals) {
            wrongUnderside += normal.y < 0.0F ? 0 : 1;
        }
    }
    CHECK(wrongTop == 0);
    CHECK(wrongUnderside == 0);
}

TEST_CASE("Every island mesh is closed, with no gaps along the rim", "[worldgen][islandmesh]")
{
    // Closure follows from the construction (shared rim vertices, rings through
    // the rim directions); the 12 islands of seed 0 keep the test fast in Debug.
    for (const BuiltIsland& built : corpus()) {
        if (built.seed != 0U) {
            continue;
        }
        INFO("island " << built.shape.island.index);
        const Closure closure = closureOf(built.mesh);
        CHECK(closure.unmatchedEdges == 0U);
        CHECK(closure.repeatedEdges == 0U);
        CHECK(closure.eulerCharacteristic == 2);
    }
    // Also at the smallest and an odd resolution.
    const IslandShape& shape = corpus().back().shape;
    for (const int n : {8, 21}) {
        const Closure closure = closureOf(buildIslandMesh(shape, n));
        CHECK(closure.unmatchedEdges == 0U);
        CHECK(closure.eulerCharacteristic == 2);
    }
}

TEST_CASE("Top and underside share the rim", "[worldgen][islandmesh]")
{
    const int resolution = WorldGenSettings{}.mesh.resolution;
    for (const BuiltIsland& built : corpus()) {
        if (built.seed != 0U) {
            continue;
        }
        // The rim: vertices at height 0 on the outline.
        const auto rimOf = [&built](const MeshData& part) {
            std::set<PositionKey> rim;
            for (const Vec3& position : part.positions) {
                if (position.y == 0.0F &&
                    outlineDistance(built.shape, engine::math::Vec2{position.x, position.z}) >
                        0.999F) {
                    rim.insert(keyOf(position));
                }
            }
            return rim;
        };
        const std::set<PositionKey> topRim = rimOf(built.mesh.top);
        CHECK(topRim.size() == 4U * static_cast<std::size_t>(resolution));
        CHECK(topRim == rimOf(built.mesh.underside));
    }
}

TEST_CASE("Triangles have at least the documented area", "[worldgen][islandmesh]")
{
    double smallest = std::numeric_limits<double>::max();
    for (const BuiltIsland& built : corpus()) {
        smallest = std::min(smallest, smallestTriangle(built.mesh));
    }
    INFO("smallest triangle in the corpus " << smallest);
    CHECK(smallest >= kMinIslandTriangleArea);

    // The worst case: the smallest islands at the highest resolution.
    WorldGenSettings tiny = withSeed(2U);
    tiny.islands.radius = engine::worldgen::FloatRange{4.0F, 4.0F};
    tiny.mesh.resolution = 256;
    const IslandShape shape = shapesOf(tiny).front();
    const double tiniest = smallestTriangle(buildIslandMesh(shape, tiny.mesh.resolution));
    INFO("smallest triangle of a 4 m island at resolution 256: " << tiniest);
    CHECK(tiniest >= kMinIslandTriangleArea);
}

TEST_CASE("Island meshes stay inside the island volume", "[worldgen][islandmesh]")
{
    int outside = 0;
    for (const BuiltIsland& built : corpus()) {
        const PlacedIsland& island = built.shape.island;
        const double radius = static_cast<double>(island.radius) * (1.0 + 1.0e-6);
        for (const MeshData* part : {&built.mesh.top, &built.mesh.underside}) {
            for (const Vec3& p : part->positions) {
                const auto x = static_cast<double>(p.x);
                const auto z = static_cast<double>(p.z);
                if (std::sqrt(x * x + z * z) > radius || p.y > island.topHeight ||
                    p.y < -island.undersideDepth) {
                    ++outside;
                }
            }
        }
        // The top rises above the rim and the underside reaches down to the tip.
        CHECK(engine::geometry::computeBounds(built.mesh.top)->max.y > 0.0F);
        CHECK(engine::geometry::computeBounds(built.mesh.underside)->min.y ==
              -island.undersideDepth);
    }
    CHECK(outside == 0);
}

TEST_CASE("Mesh hashes repeat and tell islands apart", "[worldgen][islandmesh]")
{
    std::set<std::uint64_t> hashes;
    std::size_t parts = 0;
    for (const BuiltIsland& built : corpus()) {
        const WorldGenSettings settings = withSeed(built.seed);
        const IslandMesh again = buildIslandMesh(makeIslandShape(built.shape.island, settings),
                                                 settings.mesh.resolution);
        CHECK(meshHash(again.top) == meshHash(built.mesh.top));
        CHECK(meshHash(again.underside) == meshHash(built.mesh.underside));
        CHECK(again.top.positions == built.mesh.top.positions);
        CHECK(again.underside.positions == built.mesh.underside.positions);
        hashes.insert(meshHash(built.mesh.top));
        hashes.insert(meshHash(built.mesh.underside));
        parts += 2U;
    }
    CHECK(hashes.size() == parts);
}

TEST_CASE("Island meshes match their golden hashes", "[worldgen][islandmesh]")
{
    // Hashes of positions rounded to 1/1024 m, recorded on Windows and checked
    // on every platform in CI (ADR 0003).
    const WorldGenSettings settings = withSeed(42U);
    const std::vector<IslandShape> shapes = shapesOf(settings);
    REQUIRE(shapes.size() == 12U);
    const IslandMesh first = buildIslandMesh(shapes[0], settings.mesh.resolution);
    const IslandMesh third = buildIslandMesh(shapes[2], settings.mesh.resolution);
    const IslandMesh coarse = buildIslandMesh(shapes[5], 8);
    CHECK(meshHash(first.top) == 0xEA591653E390FCC6ULL);
    CHECK(meshHash(first.underside) == 0x91F1990C91B63E8FULL);
    CHECK(meshHash(third.top) == 0x3574A09F38E56FC3ULL);
    CHECK(meshHash(third.underside) == 0x23C3DD2C7351E3C8ULL);
    CHECK(meshHash(coarse.top) == 0xAD4388B0887AABDBULL);
    CHECK(meshHash(coarse.underside) == 0x420BAD2AAE6B6272ULL);
}

TEST_CASE("meshHash rounds positions to its documented step", "[worldgen][islandmesh]")
{
    MeshData mesh;
    mesh.positions = {Vec3{1.0F, 2.0F, 3.0F}, Vec3{-4.0F, 0.5F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}};
    mesh.colors.assign(3U, Vec4{1.0F});
    mesh.indices = {0U, 1U, 2U};
    const std::uint64_t base = meshHash(mesh);

    MeshData nudged = mesh;
    nudged.positions[0].x += 1.0e-5F; // far below half a step
    CHECK(meshHash(nudged) == base);

    MeshData moved = mesh;
    moved.positions[0].x += 0.002F; // two steps
    CHECK(meshHash(moved) != base);

    MeshData reordered = mesh;
    reordered.indices = {0U, 2U, 1U};
    CHECK(meshHash(reordered) != base);

    MeshData broken = mesh;
    broken.positions[1].y = std::numeric_limits<float>::quiet_NaN();
    MeshData infinite = mesh;
    infinite.positions[1].y = std::numeric_limits<float>::infinity();
    // Non-finite coordinates hash to one fixed value instead of undefined rounding.
    CHECK(meshHash(broken) == meshHash(infinite));
    CHECK(meshHash(broken) != base);

    CHECK(meshHash(MeshData{}) == meshHash(MeshData{}));
    CHECK(meshHash(MeshData{}) != base);
}
