#include "engine/geometry/Primitives.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <limits>

using engine::geometry::computeBounds;
using engine::geometry::makeBox;
using engine::geometry::makeCheckerPlane;
using engine::geometry::makePyramid;
using engine::geometry::MeshData;
using engine::geometry::validate;
using engine::math::Vec3;
using engine::math::Vec4;

namespace {

const std::array<Vec4, 6> kSixColors{Vec4{1, 0, 0, 1}, Vec4{0, 1, 0, 1}, Vec4{0, 0, 1, 1},
                                     Vec4{1, 1, 0, 1}, Vec4{0, 1, 1, 1}, Vec4{1, 0, 1, 1}};
const std::array<Vec4, 5> kFiveColors{Vec4{1, 0, 0, 1}, Vec4{0, 1, 0, 1}, Vec4{0, 0, 1, 1},
                                      Vec4{1, 1, 0, 1}, Vec4{0, 1, 1, 1}};

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

} // namespace

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
}
