#include "engine/geometry/MeshData.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using engine::geometry::append;
using engine::geometry::computeBounds;
using engine::geometry::MeshData;
using engine::geometry::validate;
using engine::math::Vec3;
using engine::math::Vec4;

namespace {

MeshData unitTriangle()
{
    MeshData mesh;
    mesh.positions = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
    mesh.colors = {Vec4{1.0F}, Vec4{1.0F}, Vec4{1.0F}};
    mesh.indices = {0, 1, 2};
    return mesh;
}

bool mentions(const std::optional<engine::core::Error>& error, const std::string& text)
{
    return error.has_value() && error->message.find(text) != std::string::npos;
}

} // namespace

TEST_CASE("A consistent mesh and an empty mesh are valid", "[geometry][mesh]")
{
    CHECK_FALSE(validate(unitTriangle()).has_value());
    CHECK_FALSE(validate(MeshData{}).has_value());
    CHECK(MeshData{}.empty());
    CHECK(unitTriangle().triangleCount() == 1);
    CHECK(unitTriangle().vertexCount() == 3);
}

TEST_CASE("Each position needs exactly one color", "[geometry][mesh]")
{
    MeshData mesh = unitTriangle();
    mesh.colors.pop_back();
    CHECK(mentions(validate(mesh), "3 positions but 2 colors"));
}

TEST_CASE("Indices must form whole triangles", "[geometry][mesh]")
{
    MeshData mesh = unitTriangle();
    mesh.indices.push_back(0);
    CHECK(mentions(validate(mesh), "not a multiple of 3"));
}

TEST_CASE("Indices must stay inside the vertex range", "[geometry][mesh]")
{
    MeshData mesh = unitTriangle();
    mesh.indices = {0, 1, 3};
    // The message names the bad index position and value.
    CHECK(mentions(validate(mesh), "index 2 has value 3"));
}

TEST_CASE("Positions and colors must be finite", "[geometry][mesh]")
{
    MeshData mesh = unitTriangle();
    mesh.positions[1].y = std::numeric_limits<float>::quiet_NaN();
    CHECK(mentions(validate(mesh), "position 1 is not finite"));

    mesh = unitTriangle();
    mesh.colors[2].a = std::numeric_limits<float>::infinity();
    CHECK(mentions(validate(mesh), "color 2 is not finite"));
}

TEST_CASE("Bounds enclose all positions", "[geometry][mesh]")
{
    MeshData mesh = unitTriangle();
    mesh.positions.push_back({-2.0F, 5.0F, 3.0F});
    mesh.colors.emplace_back(1.0F);
    const auto bounds = computeBounds(mesh);
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == Vec3{-2.0F, 0.0F, 0.0F});
    CHECK(bounds->max == Vec3{1.0F, 5.0F, 3.0F});

    CHECK_FALSE(computeBounds(MeshData{}).has_value());
}

TEST_CASE("append shifts the indices of the appended mesh", "[geometry][mesh]")
{
    MeshData combined = unitTriangle();
    append(combined, unitTriangle());
    CHECK(combined.vertexCount() == 6);
    CHECK(combined.colors.size() == 6);
    CHECK(combined.indices == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5});
    CHECK_FALSE(validate(combined).has_value());

    append(combined, MeshData{});
    CHECK(combined.vertexCount() == 6);
}
