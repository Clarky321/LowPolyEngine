// The sky of island-preview: gradient data and the dome mesh (M3.8).
#include "Sky.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

using engine::math::Vec3;
using island_preview::buildSkyDome;
using island_preview::floatingWorldSky;
using island_preview::skyColor;
using island_preview::SkyGradient;

namespace {

bool near(const Vec3& a, const Vec3& b)
{
    return glm::length(a - b) < 1.0e-6F;
}

} // namespace

TEST_CASE("Sky colours follow the gradient stops", "[sandbox][sky]")
{
    const SkyGradient sky = floatingWorldSky();
    REQUIRE(sky.stops.size() >= 2U);
    for (std::size_t i = 1; i < sky.stops.size(); ++i) {
        CHECK(sky.stops[i - 1U].elevation < sky.stops[i].elevation);
    }
    for (const auto& stop : sky.stops) {
        CHECK(near(skyColor(sky, stop.elevation), stop.color));
        for (int channel = 0; channel < 3; ++channel) {
            CHECK(stop.color[channel] >= 0.0F);
            CHECK(stop.color[channel] <= 1.0F);
        }
    }
    // Halfway between two stops, and constant beyond the ends.
    const auto& a = sky.stops[0];
    const auto& b = sky.stops[1];
    CHECK(near(skyColor(sky, (a.elevation + b.elevation) * 0.5F), (a.color + b.color) * 0.5F));
    CHECK(near(skyColor(sky, -5.0F), sky.stops.front().color));
    CHECK(near(skyColor(sky, 5.0F), sky.stops.back().color));
    CHECK(near(skyColor(SkyGradient{}, 0.3F), Vec3{0.0F}));
}

TEST_CASE("The sky dome surrounds the camera and faces inward", "[sandbox][sky]")
{
    const SkyGradient sky = floatingWorldSky();
    constexpr int bands = 16;
    constexpr int segments = 32;
    const auto mesh = buildSkyDome(sky, bands, segments);
    CHECK_FALSE(engine::geometry::validate(mesh).has_value());
    CHECK_FALSE(mesh.hasNormals());
    CHECK(mesh.vertexCount() == 2U + static_cast<std::size_t>((bands - 1) * segments));
    CHECK(mesh.triangleCount() == static_cast<std::size_t>(2 * segments * (bands - 1)));

    int offSphere = 0;
    int horizon = 0;
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const Vec3& p = mesh.positions[i];
        if (std::abs(glm::length(p) - 1.0F) > 1.0e-5F) {
            ++offSphere;
        }
        // Each vertex has the gradient's colour for its height.
        CHECK(near(Vec3{mesh.colors[i]}, skyColor(sky, p.y)));
        CHECK(mesh.colors[i].a == 1.0F);
        if (p.y == 0.0F) {
            ++horizon;
        }
    }
    CHECK(offSphere == 0);
    // The horizon is a ring of vertices exactly at elevation 0.
    CHECK(horizon == segments);

    int facingOut = 0;
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const Vec3& a = mesh.positions[mesh.indices[3U * t]];
        const Vec3& b = mesh.positions[mesh.indices[3U * t + 1U]];
        const Vec3& c = mesh.positions[mesh.indices[3U * t + 2U]];
        const Vec3 normal = glm::cross(b - a, c - a);
        if (glm::dot(normal, (a + b + c) / 3.0F) >= 0.0F) {
            ++facingOut;
        }
    }
    CHECK(facingOut == 0);

    // An odd band count is rounded up, so there is always a horizon ring.
    const auto odd = buildSkyDome(sky, 7, 8);
    CHECK(odd.vertexCount() == 2U + 7U * 8U);
}
