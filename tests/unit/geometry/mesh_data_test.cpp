#include "engine/geometry/MeshData.hpp"

// Size arithmetic behind append() (internal header, no GPU needed).
#include "geometry/MeshLimits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using engine::geometry::append;
using engine::geometry::computeBounds;
using engine::geometry::kNormalLengthTolerance;
using engine::geometry::MeshData;
using engine::geometry::validate;
using engine::math::Vec2;
using engine::math::Vec3;
using engine::math::Vec4;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr std::uint32_t kMaxIndex = std::numeric_limits<std::uint32_t>::max();

MeshData unitTriangle()
{
    MeshData mesh;
    mesh.positions = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
    mesh.colors = {Vec4{1.0F}, Vec4{1.0F}, Vec4{1.0F}};
    mesh.indices = {0, 1, 2};
    return mesh;
}

// unitTriangle (facing +Z) with the requested optional attributes.
MeshData attributedTriangle(bool withNormals, bool withUvs)
{
    MeshData mesh = unitTriangle();
    if (withNormals) {
        mesh.normals.assign(3, Vec3{0.0F, 0.0F, 1.0F});
    }
    if (withUvs) {
        mesh.uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
    }
    return mesh;
}

// Validates a triangle whose first vertex has the given normal.
std::optional<engine::core::Error> validateWithNormal(const Vec3& normal)
{
    MeshData mesh = attributedTriangle(true, false);
    mesh.normals[0] = normal;
    return validate(mesh);
}

bool mentions(const std::optional<engine::core::Error>& error, const std::string& text)
{
    return error.has_value() && error->message.find(text) != std::string::npos;
}

// Exact comparison of all arrays: the tests copy values, they do not compute them.
bool sameContents(const MeshData& a, const MeshData& b)
{
    return a.positions == b.positions && a.colors == b.colors && a.normals == b.normals &&
           a.uvs == b.uvs && a.indices == b.indices;
}

} // namespace

// --- Required attributes and existing invariants -------------------------------

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
    mesh.positions[1].y = kNaN;
    CHECK(mentions(validate(mesh), "position 1 is not finite"));

    mesh = unitTriangle();
    mesh.colors[2].a = kInfinity;
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

// --- Optional attributes: presence -------------------------------------------

TEST_CASE("Normals and UVs are optional attributes", "[geometry][mesh]")
{
    SECTION("neither")
    {
        const MeshData mesh = attributedTriangle(false, false);
        CHECK_FALSE(mesh.hasNormals());
        CHECK_FALSE(mesh.hasUvs());
        CHECK_FALSE(validate(mesh).has_value());
    }
    SECTION("normals only")
    {
        const MeshData mesh = attributedTriangle(true, false);
        CHECK(mesh.hasNormals());
        CHECK_FALSE(mesh.hasUvs());
        CHECK_FALSE(validate(mesh).has_value());
    }
    SECTION("UVs only")
    {
        const MeshData mesh = attributedTriangle(false, true);
        CHECK_FALSE(mesh.hasNormals());
        CHECK(mesh.hasUvs());
        CHECK_FALSE(validate(mesh).has_value());
    }
    SECTION("both")
    {
        const MeshData mesh = attributedTriangle(true, true);
        CHECK(mesh.hasNormals());
        CHECK(mesh.hasUvs());
        CHECK_FALSE(validate(mesh).has_value());
    }
    SECTION("an empty mesh has neither")
    {
        CHECK_FALSE(MeshData{}.hasNormals());
        CHECK_FALSE(MeshData{}.hasUvs());
    }
}

// --- Optional attributes: normals --------------------------------------------

TEST_CASE("Normals are absent or one per position", "[geometry][mesh]")
{
    MeshData mesh = attributedTriangle(true, false);
    mesh.normals.pop_back();
    CHECK(mentions(validate(mesh), "3 positions but 2 normals (expected 0 or 3)"));

    mesh = attributedTriangle(true, false);
    mesh.normals.emplace_back(0.0F, 0.0F, 1.0F);
    CHECK(mentions(validate(mesh), "3 positions but 4 normals (expected 0 or 3)"));
}

TEST_CASE("A mesh without positions has no normals or UVs", "[geometry][mesh]")
{
    MeshData mesh;
    mesh.normals.emplace_back(0.0F, 1.0F, 0.0F);
    CHECK(mentions(validate(mesh), "0 positions but 1 normals (expected 0)"));

    mesh = MeshData{};
    mesh.uvs.emplace_back(0.0F, 0.0F);
    CHECK(mentions(validate(mesh), "0 positions but 1 uvs (expected 0)"));
}

TEST_CASE("Normals must be finite", "[geometry][mesh]")
{
    MeshData mesh = attributedTriangle(true, false);
    mesh.normals[1].x = kNaN;
    CHECK(mentions(validate(mesh), "normal 1 is not finite"));

    mesh = attributedTriangle(true, false);
    mesh.normals[2].z = kInfinity;
    CHECK(mentions(validate(mesh), "normal 2 is not finite"));

    mesh = attributedTriangle(true, false);
    mesh.normals[0].y = -kInfinity;
    CHECK(mentions(validate(mesh), "normal 0 is not finite"));
}

TEST_CASE("Normals must have unit length", "[geometry][mesh]")
{
    const float c = 1.0F / std::sqrt(3.0F);
    CHECK_FALSE(validateWithNormal({c, c, c}).has_value());
    CHECK_FALSE(validateWithNormal({0.6F, 0.8F, 0.0F}).has_value());
    CHECK_FALSE(validateWithNormal({0.0F, -1.0F, 0.0F}).has_value());

    // The message names the vertex and the measured length. validate() only
    // reports; it does not normalize.
    CHECK(mentions(validateWithNormal({0.0F, 0.0F, 0.0F}), "normal 0 has length 0,"));
    CHECK(mentions(validateWithNormal({0.0F, 2.0F, 0.0F}), "normal 0 has length 2,"));
    CHECK(mentions(validateWithNormal({0.5F, 0.0F, 0.0F}), "normal 0 has length 0.5,"));
}

TEST_CASE("The normal length tolerance is 1e-3 on both sides", "[geometry][mesh]")
{
    CHECK(kNormalLengthTolerance == 1.0e-3F);

    // Lengths exactly 1 +/- 1e-3 are not representable in float, so the checks
    // stay 1e-4 inside and outside the limit (about 800 float steps near 1).
    CHECK_FALSE(validateWithNormal({1.0009F, 0.0F, 0.0F}).has_value());
    CHECK_FALSE(validateWithNormal({0.0F, 0.9991F, 0.0F}).has_value());
    CHECK(mentions(validateWithNormal({1.0011F, 0.0F, 0.0F}),
                   "normal 0 has length 1.0011, expected 1 +/- 0.001"));
    CHECK(mentions(validateWithNormal({0.0F, 0.0F, 0.9989F}), "normal 0 has length 0.9989"));
}

// --- Optional attributes: UVs ------------------------------------------------

TEST_CASE("UVs are absent or one per position", "[geometry][mesh]")
{
    MeshData mesh = attributedTriangle(false, true);
    mesh.uvs.pop_back();
    CHECK(mentions(validate(mesh), "3 positions but 2 uvs (expected 0 or 3)"));

    mesh = attributedTriangle(false, true);
    mesh.uvs.emplace_back(0.5F, 0.5F);
    CHECK(mentions(validate(mesh), "3 positions but 4 uvs (expected 0 or 3)"));
}

TEST_CASE("UVs must be finite", "[geometry][mesh]")
{
    MeshData mesh = attributedTriangle(false, true);
    mesh.uvs[2].x = kNaN;
    CHECK(mentions(validate(mesh), "uv 2 is not finite"));

    mesh = attributedTriangle(false, true);
    mesh.uvs[1].y = -kInfinity;
    CHECK(mentions(validate(mesh), "uv 1 is not finite"));
}

TEST_CASE("UVs outside [0, 1] are valid for repeating textures", "[geometry][mesh]")
{
    MeshData mesh = attributedTriangle(false, true);
    mesh.uvs = {{-3.5F, 12.0F}, {1.0e6F, -1.0e6F}, {-0.25F, 1.25F}};
    CHECK_FALSE(validate(mesh).has_value());
}

// --- Order of reported failures ------------------------------------------------

TEST_CASE("validate reports the first failure in the documented order", "[geometry][mesh]")
{
    SECTION("color count before normal count")
    {
        MeshData mesh = attributedTriangle(true, false);
        mesh.colors.pop_back();
        mesh.normals.pop_back();
        CHECK(mentions(validate(mesh), "2 colors"));
    }
    SECTION("normal count before UV count")
    {
        MeshData mesh = attributedTriangle(true, true);
        mesh.normals.pop_back();
        mesh.uvs.pop_back();
        CHECK(mentions(validate(mesh), "2 normals"));
    }
    SECTION("UV count before index errors")
    {
        MeshData mesh = attributedTriangle(false, true);
        mesh.uvs.pop_back();
        mesh.indices = {0, 1, 7};
        CHECK(mentions(validate(mesh), "2 uvs"));
    }
    SECTION("position before normal at the same vertex")
    {
        MeshData mesh = attributedTriangle(true, false);
        mesh.positions[0].x = kNaN;
        mesh.normals[0].x = kNaN;
        CHECK(mentions(validate(mesh), "position 0 is not finite"));
    }
    SECTION("normal before UV at the same vertex")
    {
        MeshData mesh = attributedTriangle(true, true);
        mesh.normals[1] = Vec3{0.0F};
        mesh.uvs[1].x = kNaN;
        CHECK(mentions(validate(mesh), "normal 1 has length 0"));
    }
    SECTION("earlier vertex first")
    {
        MeshData mesh = attributedTriangle(true, true);
        mesh.uvs[0].y = kNaN;
        mesh.normals[2] = Vec3{0.0F};
        CHECK(mentions(validate(mesh), "uv 0 is not finite"));
    }
}

// --- append ------------------------------------------------------------------

TEST_CASE("append shifts the indices of the appended mesh", "[geometry][mesh]")
{
    MeshData combined = unitTriangle();
    REQUIRE_FALSE(append(combined, unitTriangle()).has_value());
    CHECK(combined.vertexCount() == 6);
    CHECK(combined.colors.size() == 6);
    CHECK(combined.indices == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5});
    CHECK_FALSE(combined.hasNormals());
    CHECK_FALSE(combined.hasUvs());
    CHECK_FALSE(validate(combined).has_value());
}

TEST_CASE("append offsets source indices by the original target vertex count", "[geometry][mesh]")
{
    MeshData target = unitTriangle();
    target.positions.push_back({5.0F, 5.0F, 5.0F});
    target.colors.emplace_back(1.0F);
    MeshData source = unitTriangle();
    source.indices = {2, 0, 1, 1, 0, 2};

    REQUIRE_FALSE(append(target, source).has_value());
    CHECK(target.indices == std::vector<std::uint32_t>{0, 1, 2, 6, 4, 5, 5, 4, 6});
    CHECK_FALSE(validate(target).has_value());
}

TEST_CASE("append concatenates the optional attributes both meshes have", "[geometry][mesh]")
{
    const Vec3 up{0.0F, 0.0F, 1.0F};
    const Vec3 down{0.0F, 0.0F, -1.0F};

    SECTION("normals")
    {
        MeshData target = attributedTriangle(true, false);
        MeshData source = attributedTriangle(true, false);
        source.normals.assign(3, down);
        REQUIRE_FALSE(append(target, source).has_value());
        CHECK(target.normals == std::vector<Vec3>{up, up, up, down, down, down});
        CHECK_FALSE(target.hasUvs());
        CHECK_FALSE(validate(target).has_value());
    }
    SECTION("UVs")
    {
        MeshData target = attributedTriangle(false, true);
        MeshData source = attributedTriangle(false, true);
        source.uvs = {{2.0F, 2.0F}, {3.0F, 2.0F}, {2.0F, 3.0F}};
        REQUIRE_FALSE(append(target, source).has_value());
        CHECK(target.uvs == std::vector<Vec2>{{0.0F, 0.0F},
                                              {1.0F, 0.0F},
                                              {0.0F, 1.0F},
                                              {2.0F, 2.0F},
                                              {3.0F, 2.0F},
                                              {2.0F, 3.0F}});
        CHECK_FALSE(target.hasNormals());
        CHECK_FALSE(validate(target).has_value());
    }
    SECTION("both")
    {
        MeshData target = attributedTriangle(true, true);
        MeshData source = attributedTriangle(true, true);
        source.normals.assign(3, down);
        REQUIRE_FALSE(append(target, source).has_value());
        CHECK(target.normals.size() == 6);
        CHECK(target.normals[3] == down);
        CHECK(target.uvs.size() == 6);
        CHECK(target.indices == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5});
        CHECK_FALSE(validate(target).has_value());
    }
}

TEST_CASE("append requires meshes with vertices to agree on optional attributes",
          "[geometry][mesh]")
{
    // Every combination of normals/UVs on the target and on the source.
    for (int layout = 0; layout < 16; ++layout) {
        const bool targetNormals = (layout & 1) != 0;
        const bool targetUvs = (layout & 2) != 0;
        const bool sourceNormals = (layout & 4) != 0;
        const bool sourceUvs = (layout & 8) != 0;
        CAPTURE(targetNormals, targetUvs, sourceNormals, sourceUvs);

        MeshData target = attributedTriangle(targetNormals, targetUvs);
        const MeshData before = target;
        const auto error = append(target, attributedTriangle(sourceNormals, sourceUvs));

        if (targetNormals != sourceNormals) {
            CHECK(mentions(error, targetNormals
                                      ? "the target has normals but the source has none"
                                      : "the source has normals but the target has none"));
            CHECK(sameContents(target, before));
        } else if (targetUvs != sourceUvs) {
            CHECK(mentions(error, targetUvs ? "the target has uvs but the source has none"
                                            : "the source has uvs but the target has none"));
            CHECK(sameContents(target, before));
        } else {
            REQUIRE_FALSE(error.has_value());
            CHECK(target.vertexCount() == 6);
            CHECK(target.hasNormals() == targetNormals);
            CHECK(target.hasUvs() == targetUvs);
            CHECK_FALSE(validate(target).has_value());
        }
    }
}

TEST_CASE("A mesh without vertices matches any attribute layout", "[geometry][mesh]")
{
    SECTION("empty target takes the source's layout")
    {
        MeshData target;
        const MeshData source = attributedTriangle(true, true);
        REQUIRE_FALSE(append(target, source).has_value());
        CHECK(sameContents(target, source));
        CHECK(target.hasNormals());
        CHECK(target.hasUvs());
    }
    SECTION("appending an empty mesh changes nothing")
    {
        for (const bool normals : {false, true}) {
            for (const bool uvs : {false, true}) {
                MeshData target = attributedTriangle(normals, uvs);
                const MeshData before = target;
                REQUIRE_FALSE(append(target, MeshData{}).has_value());
                CHECK(sameContents(target, before));
            }
        }
    }
    SECTION("two empty meshes")
    {
        MeshData target;
        REQUIRE_FALSE(append(target, MeshData{}).has_value());
        CHECK(sameContents(target, MeshData{}));
    }
}

TEST_CASE("A mesh can be appended to itself", "[geometry][mesh]")
{
    MeshData mesh = attributedTriangle(true, true);
    mesh.normals[2] = Vec3{0.0F, 1.0F, 0.0F};
    const MeshData original = mesh;

    REQUIRE_FALSE(append(mesh, mesh).has_value());
    CHECK(mesh.vertexCount() == 6);
    CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5});
    CHECK(mesh.normals[5] == original.normals[2]);
    CHECK(mesh.uvs[4] == original.uvs[1]);
    CHECK(mesh.positions[3] == original.positions[0]);
    CHECK_FALSE(validate(mesh).has_value());

    MeshData empty;
    REQUIRE_FALSE(append(empty, empty).has_value());
    CHECK(sameContents(empty, MeshData{}));
}

TEST_CASE("append rejects indices that would overflow 32 bits", "[geometry][mesh]")
{
    // An invalid source index near the 32-bit limit must not wrap around to a
    // small, valid-looking index.
    MeshData source = unitTriangle();
    source.indices[1] = kMaxIndex;

    MeshData target = unitTriangle();
    const MeshData before = target;
    CHECK(mentions(append(target, source),
                   "source index 1 has value 4294967295, which does not fit in 32 bits after "
                   "adding the offset 3"));
    CHECK(sameContents(target, before));

    // With offset 0 nothing wraps: the invalid index is kept, and validate()
    // still reports it.
    MeshData empty;
    REQUIRE_FALSE(append(empty, source).has_value());
    CHECK(mentions(validate(empty), "index 1 has value 4294967295"));
}

TEST_CASE("Vertex count limit of 32-bit indices", "[geometry][mesh]")
{
    using engine::geometry::detail::fitsShiftedIndex;
    using engine::geometry::detail::fitsVertexCount;
    using engine::geometry::detail::kMaxVertexCount;
    constexpr std::size_t kSizeMax = std::numeric_limits<std::size_t>::max();

    CHECK(kMaxVertexCount == 4294967295U);

    // Checked with plain numbers: real meshes this large would need > 100 GB.
    CHECK(fitsVertexCount(0, 0));
    CHECK(fitsVertexCount(0, kMaxVertexCount));
    CHECK(fitsVertexCount(kMaxVertexCount, 0));
    CHECK(fitsVertexCount(kMaxVertexCount - 10, 10));
    CHECK_FALSE(fitsVertexCount(kMaxVertexCount - 10, 11));
    CHECK_FALSE(fitsVertexCount(1, kMaxVertexCount));
    CHECK_FALSE(fitsVertexCount(kMaxVertexCount + 1, 0));
    // The sum would wrap around std::size_t; the check must not.
    CHECK_FALSE(fitsVertexCount(kSizeMax, kSizeMax));
    CHECK_FALSE(fitsVertexCount(kSizeMax, 1));

    CHECK(fitsShiftedIndex(kMaxIndex, 0));
    CHECK(fitsShiftedIndex(kMaxIndex - 1, 1));
    CHECK(fitsShiftedIndex(0, kMaxIndex));
    CHECK_FALSE(fitsShiftedIndex(kMaxIndex, 1));
    CHECK_FALSE(fitsShiftedIndex(1, kMaxIndex));
}
