// A whole generated archipelago: placement, shapes, meshes and palettes in one
// call (M3.8).
#include "engine/worldgen/Archipelago.hpp"

#include "engine/worldgen/IslandMesh.hpp"
#include "engine/worldgen/IslandPalette.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>

using engine::worldgen::Archipelago;
using engine::worldgen::generateArchipelago;
using engine::worldgen::WorldGenSettings;

namespace {

Archipelago archipelagoOf(const WorldGenSettings& settings)
{
    auto result = generateArchipelago(settings);
    REQUIRE(result.hasValue());
    return result.value();
}

} // namespace

TEST_CASE("The default archipelago has every island meshed and painted", "[worldgen][archipelago]")
{
    const WorldGenSettings settings;
    const Archipelago archipelago = archipelagoOf(settings);
    REQUIRE(archipelago.islands.size() == archipelago.layout.islands.size());
    CHECK(archipelago.islands.size() == 12U);
    CHECK(archipelago.layout.warning.empty());

    const auto palettes = engine::worldgen::choosePalettes(archipelago.layout, settings);
    const auto n = static_cast<std::uint64_t>(settings.mesh.resolution);
    std::uint64_t triangles = 0;
    for (std::size_t i = 0; i < archipelago.islands.size(); ++i) {
        const auto& island = archipelago.islands[i];
        INFO("island " << island.shape.island.index);
        CHECK(island.shape.island == archipelago.layout.islands[i]);
        CHECK(island.palette == palettes[i]);
        CHECK_FALSE(engine::geometry::validate(island.mesh).has_value());
        // Top and underside of a default mesh: 2 n^2 + 4 n (n - 1) triangles.
        CHECK(island.mesh.triangleCount() == 2U * n * n + 4U * n * (n - 1U));
        // Island-local: everything within the island's bounding circle.
        bool inside = true;
        for (const auto& p : island.mesh.positions) {
            const auto x = static_cast<double>(p.x);
            const auto z = static_cast<double>(p.z);
            inside = inside && std::sqrt(x * x + z * z) <=
                                   static_cast<double>(island.shape.island.radius) * 1.000001;
        }
        CHECK(inside);
        triangles += island.mesh.triangleCount();
    }
    CHECK(archipelago.triangleCount() == triangles);
}

TEST_CASE("The archipelago's meshes are the painted island meshes", "[worldgen][archipelago]")
{
    WorldGenSettings settings;
    settings.seed = 9U;
    settings.mesh.resolution = 12;
    const Archipelago archipelago = archipelagoOf(settings);
    const auto& island = archipelago.islands.front();
    engine::worldgen::IslandMesh parts = engine::worldgen::buildIslandMesh(island.shape, 12);
    engine::worldgen::paintIsland(parts, island.shape, island.palette.palette);
    REQUIRE_FALSE(engine::geometry::append(parts.top, parts.underside).has_value());
    CHECK(island.mesh.positions == parts.top.positions);
    CHECK(island.mesh.colors == parts.top.colors);
    CHECK(island.mesh.normals == parts.top.normals);
    CHECK(island.mesh.indices == parts.top.indices);
}

TEST_CASE("Archipelagos repeat for a seed and differ between seeds", "[worldgen][archipelago]")
{
    WorldGenSettings settings;
    settings.mesh.resolution = 12;
    const Archipelago first = archipelagoOf(settings);
    const Archipelago again = archipelagoOf(settings);
    REQUIRE(first.islands.size() == again.islands.size());
    for (std::size_t i = 0; i < first.islands.size(); ++i) {
        CHECK(first.islands[i].mesh.positions == again.islands[i].mesh.positions);
        CHECK(first.islands[i].mesh.colors == again.islands[i].mesh.colors);
        CHECK(engine::worldgen::meshHash(first.islands[i].mesh) ==
              engine::worldgen::meshHash(again.islands[i].mesh));
    }
    settings.seed = 2U;
    const Archipelago other = archipelagoOf(settings);
    CHECK(other.layout.islands != first.layout.islands);
}

TEST_CASE("Archipelago generation reports invalid settings and left-out islands",
          "[worldgen][archipelago]")
{
    WorldGenSettings invalid;
    invalid.islands.count = 0;
    const auto result = generateArchipelago(invalid);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message == "islands.count is 0, outside its range 1 to 64");

    WorldGenSettings crowded;
    crowded.seed = 42U;
    crowded.islands.count = 40;
    crowded.islands.radius = engine::worldgen::FloatRange{50.0F, 55.0F};
    crowded.islands.minHorizontalSpacing = 60.0F;
    crowded.islands.minVerticalSpacing = 60.0F;
    crowded.mesh.resolution = 8;
    const Archipelago archipelago = archipelagoOf(crowded);
    CHECK(archipelago.islands.size() == 8U);
    CHECK(archipelago.layout.warning.starts_with("placed 8 of 40 islands"));
}
