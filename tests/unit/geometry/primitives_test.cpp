#include "engine/geometry/Primitives.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using engine::geometry::computeBounds;
using engine::geometry::makeBox;
using engine::geometry::makeCheckerPlane;
using engine::geometry::makePyramid;
using engine::geometry::MeshData;
using engine::geometry::validate;
using engine::math::Vec2;
using engine::math::Vec3;
using engine::math::Vec4;

namespace {

const std::array<Vec4, 6> kSixColors{Vec4{1, 0, 0, 1}, Vec4{0, 1, 0, 1}, Vec4{0, 0, 1, 1},
                                     Vec4{1, 1, 0, 1}, Vec4{0, 1, 1, 1}, Vec4{1, 0, 1, 1}};
const std::array<Vec4, 5> kFiveColors{Vec4{1, 0, 0, 1}, Vec4{0, 1, 0, 1}, Vec4{0, 0, 1, 1},
                                      Vec4{1, 1, 0, 1}, Vec4{0, 1, 1, 1}};

// UVs of every quad corner in vertex order (MeshData of the primitives).
const std::vector<Vec2> kUnitSquare{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};

Vec3 corner(const MeshData& mesh, std::size_t triangle, std::size_t k)
{
    return mesh.positions[mesh.indices[triangle * 3 + k]];
}

// Counter-clockwise winding gives a normal pointing to the viewer.
Vec3 windingNormal(const MeshData& mesh, std::size_t triangle)
{
    const Vec3 a = corner(mesh, triangle, 0);
    return glm::cross(corner(mesh, triangle, 1) - a, corner(mesh, triangle, 2) - a);
}

Vec3 centroid(const MeshData& mesh, std::size_t triangle)
{
    return (corner(mesh, triangle, 0) + corner(mesh, triangle, 1) + corner(mesh, triangle, 2)) /
           3.0F;
}

// For convex shapes: every front face points away from an interior point.
void requireOutwardFaces(const MeshData& mesh, const Vec3& interior)
{
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        INFO("triangle " << t);
        const Vec3 normal = windingNormal(mesh, t);
        CHECK(glm::length(normal) > 1e-6F); // not degenerate
        CHECK(glm::dot(normal, centroid(mesh, t) - interior) > 0.0F);
    }
}

bool near(const Vec3& a, const Vec3& b)
{
    return glm::length(a - b) < 1e-5F;
}

// Direction from vertex `from` to vertex `to`.
Vec3 direction(const MeshData& mesh, std::size_t from, std::size_t to)
{
    return glm::normalize(mesh.positions[to] - mesh.positions[from]);
}

std::vector<Vec2> uvsOf(const MeshData& mesh, std::size_t first, std::size_t count)
{
    const auto begin = mesh.uvs.begin() + static_cast<std::ptrdiff_t>(first);
    return std::vector<Vec2>(begin, begin + static_cast<std::ptrdiff_t>(count));
}

// The normal/UV contract shared by all primitives. Every vertex has a normal
// and a UV. Per triangle, all three corners share one unit normal, which is
// perpendicular to the triangle and on the side it winds counter-clockwise
// from. The UVs wind the same way, so a texture is not mirrored seen from the
// front.
void requireFlatNormalsAndUvs(const MeshData& mesh)
{
    REQUIRE(mesh.hasNormals());
    REQUIRE(mesh.hasUvs());
    REQUIRE(mesh.normals.size() == mesh.vertexCount());
    REQUIRE(mesh.uvs.size() == mesh.vertexCount());
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        INFO("triangle " << t);
        const std::uint32_t i0 = mesh.indices[t * 3];
        const std::uint32_t i1 = mesh.indices[t * 3 + 1];
        const std::uint32_t i2 = mesh.indices[t * 3 + 2];
        const Vec3 normal = mesh.normals[i0];
        CHECK(mesh.normals[i1] == normal);
        CHECK(mesh.normals[i2] == normal);
        CHECK(std::abs(glm::length(normal) - 1.0F) < 1e-6F);

        const Vec3 edge1 = mesh.positions[i1] - mesh.positions[i0];
        const Vec3 edge2 = mesh.positions[i2] - mesh.positions[i0];
        CHECK(std::abs(glm::dot(normal, glm::normalize(edge1))) < 1e-5F);
        CHECK(std::abs(glm::dot(normal, glm::normalize(edge2))) < 1e-5F);
        CHECK(glm::dot(normal, glm::cross(edge1, edge2)) > 0.0F);

        const Vec2 uv1 = mesh.uvs[i1] - mesh.uvs[i0];
        const Vec2 uv2 = mesh.uvs[i2] - mesh.uvs[i0];
        CHECK(uv1.x * uv2.y - uv1.y * uv2.x > 0.0F);
    }
}

} // namespace

// --- Geometry and colors -----------------------------------------------------

TEST_CASE("makeBox builds 6 flat faces with outward winding", "[geometry][primitives]")
{
    const MeshData box = makeBox({1.0F, 2.0F, 3.0F}, kSixColors);
    REQUIRE_FALSE(validate(box).has_value());
    CHECK(box.vertexCount() == 24);
    CHECK(box.triangleCount() == 12);
    requireOutwardFaces(box, Vec3{0.0F});

    const auto bounds = computeBounds(box);
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == Vec3{-1.0F, -2.0F, -3.0F});
    CHECK(bounds->max == Vec3{1.0F, 2.0F, 3.0F});
}

TEST_CASE("makeBox colors each face in BoxFace order", "[geometry][primitives]")
{
    const MeshData box = makeBox(Vec3{1.0F}, kSixColors);
    const std::array<Vec3, 6> normals{Vec3{1, 0, 0},  Vec3{-1, 0, 0}, Vec3{0, 1, 0},
                                      Vec3{0, -1, 0}, Vec3{0, 0, 1},  Vec3{0, 0, -1}};
    for (std::size_t t = 0; t < box.triangleCount(); ++t) {
        const std::size_t face = t / 2;
        INFO("triangle " << t);
        CHECK(glm::normalize(windingNormal(box, t)) == normals[face]);
        for (std::size_t k = 0; k < 3; ++k) {
            CHECK(box.colors[box.indices[t * 3 + k]] == kSixColors[face]);
        }
    }
}

TEST_CASE("makePyramid builds 4 sides and a base with outward winding", "[geometry][primitives]")
{
    const MeshData pyramid = makePyramid(2.0F, 3.0F, kFiveColors);
    REQUIRE_FALSE(validate(pyramid).has_value());
    CHECK(pyramid.vertexCount() == 16);
    CHECK(pyramid.triangleCount() == 6);
    requireOutwardFaces(pyramid, Vec3{0.0F, 0.5F, 0.0F});

    const auto bounds = computeBounds(pyramid);
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == Vec3{-2.0F, 0.0F, -2.0F});
    CHECK(bounds->max == Vec3{2.0F, 3.0F, 2.0F});

    // The first side faces +X and has the first color.
    CHECK(windingNormal(pyramid, 0).x > 0.0F);
    CHECK(pyramid.colors[pyramid.indices[0]] == kFiveColors[0]);
}

TEST_CASE("makeCheckerPlane builds an upward-facing grid with alternating colors",
          "[geometry][primitives]")
{
    const Vec4 a{1, 1, 1, 1};
    const Vec4 b{0, 0, 0, 1};
    const MeshData plane = makeCheckerPlane(4, 2.5F, a, b);
    REQUIRE_FALSE(validate(plane).has_value());
    CHECK(plane.vertexCount() == 16 * 4);
    CHECK(plane.triangleCount() == 16 * 2);
    for (std::size_t t = 0; t < plane.triangleCount(); ++t) {
        const Vec3 normal = windingNormal(plane, t);
        CHECK(normal.y > 0.0F);
        CHECK(normal.x == 0.0F);
        CHECK(normal.z == 0.0F);
    }

    const auto bounds = computeBounds(plane);
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == Vec3{-5.0F, 0.0F, -5.0F});
    CHECK(bounds->max == Vec3{5.0F, 0.0F, 5.0F});

    // Neighbor cells differ: cell 0 is colorA, cell 1 colorB (4 vertices per cell).
    CHECK(plane.colors[0] == a);
    CHECK(plane.colors[4] == b);
    // Cell 0 is the one at the -X/-Z corner (its last vertex is that corner).
    CHECK(plane.positions[3] == Vec3{-5.0F, 0.0F, -5.0F});
}

TEST_CASE("Invalid primitive sizes give an empty mesh", "[geometry][primitives]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(makeBox({0.0F, 1.0F, 1.0F}, kSixColors).empty());
    CHECK(makeBox({1.0F, -1.0F, 1.0F}, kSixColors).empty());
    CHECK(makeBox({1.0F, 1.0F, nan}, kSixColors).empty());
    CHECK(makePyramid(0.0F, 1.0F, kFiveColors).empty());
    CHECK(makePyramid(1.0F, nan, kFiveColors).empty());
    CHECK(makeCheckerPlane(0, 1.0F, Vec4{1}, Vec4{0}).empty());
    CHECK(makeCheckerPlane(engine::geometry::kMaxCheckerCells + 1, 1.0F, Vec4{1}, Vec4{0}).empty());
    CHECK(makeCheckerPlane(4, -1.0F, Vec4{1}, Vec4{0}).empty());

    // An empty result has no attributes at all.
    const MeshData empty = makePyramid(-1.0F, 1.0F, kFiveColors);
    CHECK(empty.vertexCount() == 0);
    CHECK_FALSE(empty.hasNormals());
    CHECK_FALSE(empty.hasUvs());
}

// --- Normals and UVs ---------------------------------------------------------

TEST_CASE("Every primitive has flat outward normals and unmirrored UVs", "[geometry][primitives]")
{
    SECTION("box")
    {
        requireFlatNormalsAndUvs(makeBox({1.0F, 2.0F, 3.0F}, kSixColors));
    }
    SECTION("pyramid")
    {
        requireFlatNormalsAndUvs(makePyramid(2.0F, 3.0F, kFiveColors));
    }
    SECTION("checker plane")
    {
        requireFlatNormalsAndUvs(makeCheckerPlane(3, 1.5F, Vec4{1}, Vec4{0}));
    }
}

TEST_CASE("makeBox faces have axis normals and map the unit square", "[geometry][primitives]")
{
    // A non-cubic box: UVs do not depend on the face size.
    const MeshData box = makeBox({1.0F, 2.0F, 3.0F}, kSixColors);
    const Vec3 px{1, 0, 0};
    const Vec3 py{0, 1, 0};
    const Vec3 pz{0, 0, 1};
    struct Expected {
        Vec3 normal;
        Vec3 u;
        Vec3 v;
    };
    // BoxFace order. Side faces have V = +Y; top U = +X, V = -Z; bottom U = +X, V = +Z.
    const std::array<Expected, 6> faces{{
        {px, -pz, py},
        {-px, pz, py},
        {py, px, -pz},
        {-py, px, pz},
        {pz, px, py},
        {-pz, -px, py},
    }};
    for (std::size_t face = 0; face < faces.size(); ++face) {
        INFO("face " << face);
        const std::size_t first = face * 4;
        for (std::size_t k = 0; k < 4; ++k) {
            CHECK(box.normals[first + k] == faces[face].normal);
        }
        CHECK(uvsOf(box, first, 4) == kUnitSquare);
        // Corner order (0, 0), (1, 0), (1, 1), (0, 1): U along 0 -> 1, V along 0 -> 3.
        CHECK(near(direction(box, first, first + 1), faces[face].u));
        CHECK(near(direction(box, first, first + 3), faces[face].v));
    }
}

TEST_CASE("makePyramid has slanted side normals and triangle UVs", "[geometry][primitives]")
{
    const float halfBase = 2.0F;
    const float height = 3.0F;
    const MeshData pyramid = makePyramid(halfBase, height, kFiveColors);

    // A side's normal is (height, halfBase) normalized in its vertical plane.
    const std::array<Vec3, 4> sideNormals{
        glm::normalize(Vec3{height, halfBase, 0.0F}),
        glm::normalize(Vec3{-height, halfBase, 0.0F}),
        glm::normalize(Vec3{0.0F, halfBase, height}),
        glm::normalize(Vec3{0.0F, halfBase, -height}),
    };
    const std::vector<Vec2> sideUvs{{0.0F, 0.0F}, {1.0F, 0.0F}, {0.5F, 1.0F}};
    for (std::size_t side = 0; side < sideNormals.size(); ++side) {
        INFO("side " << side);
        const std::size_t first = side * 3;
        for (std::size_t k = 0; k < 3; ++k) {
            CHECK(near(pyramid.normals[first + k], sideNormals[side]));
        }
        CHECK(uvsOf(pyramid, first, 3) == sideUvs);
        // The apex carries UV (0.5, 1).
        CHECK(pyramid.positions[first + 2] == Vec3{0.0F, height, 0.0F});
    }

    // The base faces -Y; seen from below, U = +X and V = +Z.
    const std::size_t base = 12;
    for (std::size_t k = 0; k < 4; ++k) {
        CHECK(pyramid.normals[base + k] == Vec3{0.0F, -1.0F, 0.0F});
    }
    CHECK(uvsOf(pyramid, base, 4) == kUnitSquare);
    CHECK(near(direction(pyramid, base, base + 1), Vec3{1, 0, 0}));
    CHECK(near(direction(pyramid, base, base + 3), Vec3{0, 0, 1}));
}

TEST_CASE("makeCheckerPlane faces up with one UV unit per cell", "[geometry][primitives]")
{
    const int cellsPerSide = 4;
    const float cellSize = 2.5F;
    const float half = 0.5F * static_cast<float>(cellsPerSide) * cellSize;
    const MeshData plane = makeCheckerPlane(cellsPerSide, cellSize, Vec4{1}, Vec4{0});

    for (std::size_t i = 0; i < plane.vertexCount(); ++i) {
        INFO("vertex " << i);
        const Vec3& p = plane.positions[i];
        CHECK(plane.normals[i] == Vec3{0.0F, 1.0F, 0.0F});
        // U = +X and V = -Z, both measured in cells from the (-X, +Z) corner.
        const Vec2 expected{(p.x + half) / cellSize, (half - p.z) / cellSize};
        CHECK(std::abs(plane.uvs[i].x - expected.x) < 1e-5F);
        CHECK(std::abs(plane.uvs[i].y - expected.y) < 1e-5F);
        // Whole numbers in [0, cellsPerSide].
        CHECK(plane.uvs[i].x == std::round(plane.uvs[i].x));
        CHECK(plane.uvs[i].y == std::round(plane.uvs[i].y));
        const auto limit = static_cast<float>(cellsPerSide);
        CHECK((plane.uvs[i].x >= 0.0F && plane.uvs[i].x <= limit));
        CHECK((plane.uvs[i].y >= 0.0F && plane.uvs[i].y <= limit));
    }
    // Each cell spans exactly one UV unit.
    for (std::size_t cell = 0; cell < plane.vertexCount() / 4; ++cell) {
        const Vec2 span = plane.uvs[cell * 4 + 2] - plane.uvs[cell * 4];
        CHECK(span == Vec2{1.0F, 1.0F});
    }

    // The two extreme corners.
    bool foundOrigin = false;
    bool foundFarCorner = false;
    for (std::size_t i = 0; i < plane.vertexCount(); ++i) {
        if (plane.positions[i] == Vec3{-half, 0.0F, half}) {
            foundOrigin = true;
            CHECK(plane.uvs[i] == Vec2{0.0F, 0.0F});
        }
        if (plane.positions[i] == Vec3{half, 0.0F, -half}) {
            foundFarCorner = true;
            CHECK(plane.uvs[i] == Vec2{4.0F, 4.0F});
        }
    }
    CHECK(foundOrigin);
    CHECK(foundFarCorner);
}

TEST_CASE("Primitive normals stay valid for very small and very large sizes",
          "[geometry][primitives]")
{
    // A float cross product of the pyramid edges would underflow to zero (or
    // overflow to infinity) at these sizes and give NaN normals.
    for (const float size : {1.0e-30F, 1.0e30F}) {
        INFO("size " << size);
        const MeshData pyramid = makePyramid(size, size, kFiveColors);
        CHECK_FALSE(validate(pyramid).has_value());
        CHECK(near(pyramid.normals[0], glm::normalize(Vec3{1.0F, 1.0F, 0.0F})));

        CHECK_FALSE(validate(makeBox(Vec3{size}, kSixColors)).has_value());
        CHECK_FALSE(validate(makeCheckerPlane(2, size, Vec4{1}, Vec4{0})).has_value());
    }
}
