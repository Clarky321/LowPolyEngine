#include "engine/camera/FreeFlyController.hpp"

#include "engine/platform/Input.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::camera {

namespace {

constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;

float axis(bool positive, bool negative) noexcept
{
    return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F);
}

bool isFinite(const math::Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

FreeFlyController::FreeFlyController(const FreeFlySettings& settings) noexcept
    : settings_(settings),
      moveSpeed_(std::clamp(settings.moveSpeed, settings.minMoveSpeed, settings.maxMoveSpeed))
{
}

void FreeFlyController::update(Camera& camera, const FlyInput& input, float deltaSeconds) noexcept
{
    if (std::isfinite(input.speedSteps) && input.speedSteps != 0.0F) {
        moveSpeed_ = std::clamp(moveSpeed_ * std::pow(settings_.speedStepFactor, input.speedSteps),
                                settings_.minMoveSpeed, settings_.maxMoveSpeed);
    }

    if (std::isfinite(input.look.x) && std::isfinite(input.look.y)) {
        // Mouse right turns right (negative yaw); mouse down looks down.
        camera.yawRadians -= input.look.x * settings_.lookRadiansPerPixel;
        camera.pitchRadians -= input.look.y * settings_.lookRadiansPerPixel;
    }
    // Keep yaw in [-pi, pi] so it never loses precision by growing without bound.
    camera.yawRadians =
        std::isfinite(camera.yawRadians) ? std::remainder(camera.yawRadians, kTwoPi) : 0.0F;
    camera.pitchRadians = std::isfinite(camera.pitchRadians)
                              ? std::clamp(camera.pitchRadians, -kMaxPitchRadians, kMaxPitchRadians)
                              : 0.0F;

    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || !isFinite(input.move)) {
        return;
    }
    math::Vec3 direction = forwardDirection(camera) * input.move.z +
                           rightDirection(camera) * input.move.x + kWorldUp * input.move.y;
    const float length = glm::length(direction);
    if (length < 1e-6F) {
        return;
    }
    if (length > 1.0F) {
        direction /= length;
    }
    const float speed = moveSpeed_ * (input.fast ? settings_.fastMultiplier : 1.0F);
    camera.position += direction * (speed * deltaSeconds);
}

FlyInput readFlyInput(const platform::InputState& input, bool mouseLook) noexcept
{
    using platform::Key;
    const auto down = [&input](Key a, Key b) { return input.isDown(a) || input.isDown(b); };

    FlyInput fly;
    fly.move.x = axis(down(Key::D, Key::Right), down(Key::A, Key::Left));
    fly.move.y = axis(down(Key::E, Key::Space), down(Key::Q, Key::LeftCtrl));
    fly.move.z = axis(down(Key::W, Key::Up), down(Key::S, Key::Down));
    fly.fast = down(Key::LeftShift, Key::RightShift);
    if (mouseLook) {
        fly.look = math::Vec2{input.mouseDelta().x, input.mouseDelta().y};
    }
    fly.speedSteps = input.wheelSteps();
    return fly;
}

} // namespace engine::camera
