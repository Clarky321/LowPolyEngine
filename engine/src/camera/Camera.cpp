#include "engine/camera/Camera.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace engine::camera {

namespace {

constexpr float kMinFovRadians = 0.0174533F; // 1 degree
constexpr float kMaxFovRadians = 3.1241393F; // 179 degrees
constexpr float kMinNearPlane = 0.001F;

float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float clampedPitch(const Camera& camera) noexcept
{
    return std::clamp(finiteOr(camera.pitchRadians, 0.0F), -kMaxPitchRadians, kMaxPitchRadians);
}

} // namespace

math::Vec3 forwardDirection(const Camera& camera) noexcept
{
    const float yaw = finiteOr(camera.yawRadians, 0.0F);
    const float pitch = clampedPitch(camera);
    const float horizontal = std::cos(pitch);
    return {-std::sin(yaw) * horizontal, std::sin(pitch), -std::cos(yaw) * horizontal};
}

math::Vec3 rightDirection(const Camera& camera) noexcept
{
    const float yaw = finiteOr(camera.yawRadians, 0.0F);
    return {std::cos(yaw), 0.0F, -std::sin(yaw)};
}

math::Vec3 upDirection(const Camera& camera) noexcept
{
    return glm::cross(rightDirection(camera), forwardDirection(camera));
}

math::Mat4 viewMatrix(const Camera& camera)
{
    // upDirection is exactly perpendicular to forward, so lookAt never degenerates.
    const math::Vec3 forward = forwardDirection(camera);
    return glm::lookAt(camera.position, camera.position + forward, upDirection(camera));
}

math::Mat4 projectionMatrix(const Camera& camera, float aspect)
{
    const float fov =
        std::clamp(finiteOr(camera.verticalFovRadians, 1.0F), kMinFovRadians, kMaxFovRadians);
    const float nearPlane = std::max(finiteOr(camera.nearPlane, kMinNearPlane), kMinNearPlane);
    float farPlane = finiteOr(camera.farPlane, nearPlane * 1000.0F);
    if (farPlane <= nearPlane * 1.001F) {
        farPlane = nearPlane * 1000.0F;
    }
    if (!std::isfinite(aspect) || aspect <= 0.0F) {
        aspect = 1.0F;
    }
    return glm::perspective(fov, aspect, nearPlane, farPlane);
}

float aspectRatio(int width, int height) noexcept
{
    if (width <= 0 || height <= 0) {
        return 1.0F;
    }
    return static_cast<float>(width) / static_cast<float>(height);
}

} // namespace engine::camera
