// The files island-preview loads from assets/ (M2.7, M2.10). island-preview
// only logs a broken asset and goes on, so these headless checks keep the
// committed files loadable in CI, where the GPU tier does not run.

#include "engine/core/Image.hpp"
#include "engine/geometry/ObjImport.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>

using engine::math::Vec3;

namespace {

// Set by tests/CMakeLists.txt.
const std::filesystem::path kAssets{LPE_ASSET_DIRECTORY};

} // namespace

TEST_CASE("The scene textures decode", "[assets]")
{
    const auto crate = engine::core::readPng(kAssets / "textures" / "crate.png");
    REQUIRE(crate.hasValue());
    CHECK(crate.value().width == 16);
    CHECK(crate.value().height == 16);

    const auto lighthouse = engine::core::readPng(kAssets / "textures" / "lighthouse.png");
    REQUIRE(lighthouse.hasValue());
    CHECK(lighthouse.value().width == 32);
    CHECK(lighthouse.value().height == 32);
}

TEST_CASE("The lighthouse model loads as a closed, outward-facing mesh", "[assets]")
{
    const auto loaded = engine::geometry::readObj(kAssets / "models" / "lighthouse.obj");
    if (!loaded) {
        FAIL(loaded.error().message);
    }
    const auto& obj = loaded.value();
    const auto& mesh = obj.mesh;
    // Written in the supported subset only: no warning at startup.
    CHECK(obj.ignoredStatements == 0);
    REQUIRE(mesh.hasUvs());
    REQUIRE(mesh.hasNormals());
    CHECK(mesh.triangleCount() > 0);

    for (const auto& uv : mesh.uvs) {
        CHECK((uv.x >= 0.0F && uv.x <= 1.0F && uv.y >= 0.0F && uv.y <= 1.0F));
    }
    // Base on the ground at the origin, 7.8 m tall, at most 1.8 m from the axis.
    for (const Vec3& position : mesh.positions) {
        CHECK((position.y >= 0.0F && position.y <= 7.8F));
        CHECK(glm::length(Vec3{position.x, 0.0F, position.z}) <= 1.8F + 1.0e-4F);
    }
    // Front faces are counter-clockwise (back faces are culled), and the given
    // normals agree with that winding.
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const Vec3& a = mesh.positions[mesh.indices[3 * t]];
        const Vec3& b = mesh.positions[mesh.indices[3 * t + 1]];
        const Vec3& c = mesh.positions[mesh.indices[3 * t + 2]];
        const Vec3 winding = glm::cross(b - a, c - a);
        CHECK(glm::dot(winding, mesh.normals[mesh.indices[3 * t]]) > 0.0F);
    }
}
