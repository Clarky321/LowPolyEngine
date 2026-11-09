#include "engine/camera/FreeFlyController.hpp"
#include "engine/platform/Input.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>

using Catch::Approx;
using engine::camera::Camera;
using engine::camera::FlyInput;
using engine::camera::FreeFlyController;
using engine::camera::FreeFlySettings;
using engine::camera::readFlyInput;
using engine::math::Vec2;
using engine::math::Vec3;
using engine::platform::InputState;
using engine::platform::Key;

namespace {

FlyInput moving(const Vec3& move, bool fast = false)
{
    FlyInput input;
    input.move = move;
    input.fast = fast;
    return input;
}

FreeFlySettings speed10()
{
    FreeFlySettings settings;
    settings.moveSpeed = 10.0F;
    settings.fastMultiplier = 4.0F;
    return settings;
}

} // namespace

TEST_CASE("Moving forward follows the view direction at the set speed", "[camera][fly]")
{
    FreeFlyController controller(speed10());
    Camera camera;
    controller.update(camera, moving({0, 0, 1}), 0.5F);
    CHECK(camera.position.x == Approx(0.0F).margin(1e-5));
    CHECK(camera.position.z == Approx(-5.0F));

    // Looking 90 degrees left, forward is -X.
    camera = Camera{};
    camera.yawRadians = std::numbers::pi_v<float> / 2.0F;
    controller.update(camera, moving({0, 0, 1}), 1.0F);
    CHECK(camera.position.x == Approx(-10.0F));
}

TEST_CASE("Diagonal movement is not faster than straight movement", "[camera][fly]")
{
    FreeFlyController controller(speed10());
    Camera camera;
    controller.update(camera, moving({1, 1, 1}), 1.0F);
    CHECK(glm::length(camera.position) == Approx(10.0F));
}

TEST_CASE("Vertical movement uses world up regardless of pitch", "[camera][fly]")
{
    FreeFlyController controller(speed10());
    Camera camera;
    camera.pitchRadians = -1.0F;
    controller.update(camera, moving({0, 1, 0}), 1.0F);
    CHECK(camera.position.y == Approx(10.0F));
    CHECK(camera.position.x == Approx(0.0F).margin(1e-5));
    CHECK(camera.position.z == Approx(0.0F).margin(1e-5));
}

TEST_CASE("Fast movement multiplies the speed", "[camera][fly]")
{
    FreeFlyController controller(speed10());
    Camera camera;
    controller.update(camera, moving({1, 0, 0}, true), 1.0F);
    CHECK(camera.position.x == Approx(40.0F));
}

TEST_CASE("Zero, negative and non-finite time steps do not move", "[camera][fly]")
{
    FreeFlyController controller(speed10());
    Camera camera;
    for (const float dt : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()}) {
        controller.update(camera, moving({0, 0, 1}), dt);
    }
    CHECK(camera.position == Vec3{0.0F});
}

TEST_CASE("Mouse right turns right, mouse down looks down", "[camera][fly]")
{
    FreeFlySettings settings;
    settings.lookRadiansPerPixel = 0.01F;
    FreeFlyController controller(settings);
    Camera camera;
    FlyInput input;
    input.look = Vec2{10.0F, 5.0F};
    controller.update(camera, input, 0.016F);
    CHECK(camera.yawRadians == Approx(-0.1F));
    CHECK(camera.pitchRadians == Approx(-0.05F));
    // Turning right means the view direction gains +X.
    CHECK(forwardDirection(camera).x > 0.0F);
}

TEST_CASE("Pitch is clamped and yaw stays within one turn", "[camera][fly]")
{
    FreeFlySettings settings;
    settings.lookRadiansPerPixel = 0.01F;
    FreeFlyController controller(settings);
    Camera camera;
    FlyInput input;
    input.look = Vec2{1000.0F, -100000.0F};
    controller.update(camera, input, 0.016F);
    CHECK(camera.pitchRadians == Approx(engine::camera::kMaxPitchRadians));
    CHECK(std::abs(camera.yawRadians) <= std::numbers::pi_v<float> + 1e-5F);

    input.look = Vec2{std::numeric_limits<float>::quiet_NaN(), 0.0F};
    controller.update(camera, input, 0.016F);
    CHECK(std::isfinite(camera.yawRadians));
}

TEST_CASE("Wheel steps change the speed within limits", "[camera][fly]")
{
    FreeFlySettings settings = speed10();
    settings.speedStepFactor = 2.0F;
    settings.minMoveSpeed = 1.0F;
    settings.maxMoveSpeed = 50.0F;
    FreeFlyController controller(settings);
    Camera camera;

    FlyInput input;
    input.speedSteps = 1.0F;
    controller.update(camera, input, 0.016F);
    CHECK(controller.moveSpeed() == Approx(20.0F));

    input.speedSteps = 10.0F;
    controller.update(camera, input, 0.016F);
    CHECK(controller.moveSpeed() == Approx(50.0F));

    input.speedSteps = -20.0F;
    controller.update(camera, input, 0.016F);
    CHECK(controller.moveSpeed() == Approx(1.0F));
}

TEST_CASE("readFlyInput maps the default keys", "[camera][fly][input]")
{
    InputState input;
    input.setKey(Key::W, true);
    input.setKey(Key::A, true);
    input.setKey(Key::E, true);
    input.setKey(Key::LeftShift, true);
    input.addWheelSteps(2.0F);
    const FlyInput fly = readFlyInput(input, false);
    CHECK(fly.move == Vec3{-1.0F, 1.0F, 1.0F});
    CHECK(fly.fast);
    CHECK(fly.speedSteps == 2.0F);

    // Opposite keys cancel out.
    input.setKey(Key::S, true);
    input.setKey(Key::Q, true);
    CHECK(readFlyInput(input, false).move == Vec3{-1.0F, 0.0F, 0.0F});
}

TEST_CASE("Mouse movement only turns the camera while mouse look is active", "[camera][fly][input]")
{
    InputState input;
    input.addMouseMotion(4.0F, -2.0F);
    CHECK(readFlyInput(input, false).look == Vec2{0.0F});
    CHECK(readFlyInput(input, true).look == Vec2{4.0F, -2.0F});
}
