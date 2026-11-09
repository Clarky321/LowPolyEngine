#pragma once

#include "engine/core/Math.hpp"

namespace engine::camera {

// Pitch stops just short of straight up/down, where yaw would become undefined.
inline constexpr float kMaxPitchRadians = 1.5533430F; // 89 degrees

// A perspective camera oriented by yaw and pitch (no roll).
//
// yaw rotates about world +Y: 0 looks along -Z, positive values turn left
// (counter-clockwise seen from above). pitch is the angle above the horizon.
struct Camera {
    math::Vec3 position{0.0F};
    float yawRadians = 0.0F;
    float pitchRadians = 0.0F;
    float verticalFovRadians = 1.0471976F; // 60 degrees
    float nearPlane = 0.1F;
    float farPlane = 1000.0F;
};

inline constexpr math::Vec3 kWorldUp{0.0F, 1.0F, 0.0F};

// Unit view direction.
[[nodiscard]] math::Vec3 forwardDirection(const Camera& camera) noexcept;
// Unit horizontal direction to the right of the view.
[[nodiscard]] math::Vec3 rightDirection(const Camera& camera) noexcept;
// Unit direction up in the image, perpendicular to forward and right.
[[nodiscard]] math::Vec3 upDirection(const Camera& camera) noexcept;

// World-to-camera transform: the camera sits at the origin looking along -Z.
[[nodiscard]] math::Mat4 viewMatrix(const Camera& camera);

// OpenGL perspective projection (clip depth [-1, 1]). Out-of-range values are
// made usable instead of producing NaN: the field of view is clamped to
// [1, 179] degrees, near to at least 0.001, far to beyond near, and a
// non-positive or non-finite aspect ratio becomes 1.
[[nodiscard]] math::Mat4 projectionMatrix(const Camera& camera, float aspectRatio);

// width / height, or 1 for a zero or negative size (e.g. a minimized window).
[[nodiscard]] float aspectRatio(int width, int height) noexcept;

} // namespace engine::camera
