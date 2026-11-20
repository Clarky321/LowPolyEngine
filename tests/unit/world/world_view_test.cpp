// How the generated world is looked at (engine/world/WorldView.hpp and
// skyView, moved from island-preview in M5.3): the camera looking at a point,
// the overview that shows the whole archipelago, the sky's view and the sun.

#include "engine/camera/Camera.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/world/Sky.hpp"
#include "engine/world/WorldView.hpp"
#include "engine/worldgen/Archipelago.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

using engine::math::Mat4;
using engine::math::Vec3;
using engine::math::Vec4;

namespace {

bool near(const Vec3& a, const Vec3& b, float tolerance = 1.0e-5F)
{
    return glm::length(a - b) < tolerance;
}

bool isFinite(const engine::camera::Camera& camera)
{
    return std::isfinite(camera.position.x) && std::isfinite(camera.position.y) &&
           std::isfinite(camera.position.z) && std::isfinite(camera.yawRadians) &&
           std::isfinite(camera.pitchRadians);
}

} // namespace

TEST_CASE("A camera looking at a point faces it", "[world][view]")
{
    const Vec3 position{3.0F, 4.0F, -2.0F};
    const std::array<Vec3, 6> targets{
        Vec3{0.0F, 0.0F, 0.0F},   Vec3{3.0F, 4.0F, -12.0F}, Vec3{13.0F, 4.0F, -2.0F},
        Vec3{-7.0F, -1.0F, 5.0F}, Vec3{3.0F, 40.0F, -1.0F}, Vec3{2.0F, -30.0F, -2.5F},
    };
    for (const Vec3& target : targets) {
        INFO("target " << target.x << ", " << target.y << ", " << target.z);
        const engine::camera::Camera camera = engine::world::cameraLookingAt(position, target);
        CHECK(camera.position == position);
        CHECK(near(engine::camera::forwardDirection(camera), glm::normalize(target - position)));
    }
}

TEST_CASE("The overview shows every island of the default archipelago", "[world][view]")
{
    const auto generated =
        engine::worldgen::generateArchipelago(engine::worldgen::WorldGenSettings{});
    REQUIRE(generated.hasValue());
    const engine::worldgen::Archipelago& archipelago = generated.value();
    REQUIRE(!archipelago.islands.empty());

    const engine::camera::Camera camera = engine::world::overviewCamera(archipelago);
    REQUIRE(isFinite(camera));
    // Every island's anchor lies inside the view, for island-preview's window
    // (16:9) and for a narrower viewport like the editor's.
    for (const float aspect : {16.0F / 9.0F, 1.3F}) {
        const Mat4 clip =
            engine::camera::projectionMatrix(camera, aspect) * engine::camera::viewMatrix(camera);
        for (const auto& island : archipelago.islands) {
            const Vec4 p = clip * Vec4{island.shape.island.anchor, 1.0F};
            INFO("aspect " << aspect << ", island at " << island.shape.island.anchor.x << ", "
                           << island.shape.island.anchor.z);
            REQUIRE(p.w > 0.0F);
            CHECK(std::abs(p.x / p.w) < 1.0F);
            CHECK(std::abs(p.y / p.w) < 1.0F);
            CHECK(std::abs(p.z / p.w) < 1.0F);
        }
    }
    // Repeatable.
    const engine::camera::Camera again = engine::world::overviewCamera(archipelago);
    CHECK(again.position == camera.position);
    CHECK(again.yawRadians == camera.yawRadians);
    CHECK(again.pitchRadians == camera.pitchRadians);
}

TEST_CASE("The overview of an empty archipelago is still a usable camera", "[world][view]")
{
    const engine::camera::Camera camera =
        engine::world::overviewCamera(engine::worldgen::Archipelago{});
    CHECK(isFinite(camera));
}

TEST_CASE("The sky is drawn with the camera's rotation only", "[world][view][sky]")
{
    engine::camera::Camera camera;
    camera.position = {120.0F, -35.0F, 48.0F};
    camera.yawRadians = 0.7F;
    camera.pitchRadians = -0.3F;
    engine::camera::Camera atOrigin = camera;
    atOrigin.position = Vec3{0.0F};

    const Mat4 sky = engine::world::skyView(engine::camera::viewMatrix(camera));
    const Mat4 expected = engine::camera::viewMatrix(atOrigin);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(std::abs(sky[column][row] - expected[column][row]) < 1.0e-5F);
        }
    }
}

TEST_CASE("The archipelago's sunlight is a valid light from above", "[world][view]")
{
    const engine::render::LightDesc sun = engine::world::archipelagoSunlight();
    CHECK_FALSE(engine::render::validate(sun).has_value());
    CHECK(sun.direction.y < 0.0F);
}
