#pragma once

#include "engine/camera/Camera.hpp"
#include "engine/core/Math.hpp"

namespace engine::platform {
class InputState;
}

namespace engine::camera {

// What the user asks the camera to do this frame, independent of the device.
struct FlyInput {
    // x: right, y: world up, z: forward along the view. Each in [-1, 1].
    math::Vec3 move{0.0F};
    // Pixels of mouse movement to turn by: x to the right, y downward.
    math::Vec2 look{0.0F};
    bool fast = false;
    // Mouse wheel steps; positive makes movement faster.
    float speedSteps = 0.0F;
};

struct FreeFlySettings {
    // World units per second.
    float moveSpeed = 10.0F;
    float fastMultiplier = 4.0F;
    float lookRadiansPerPixel = 0.0025F;
    // Speed factor per wheel step.
    float speedStepFactor = 1.25F;
    float minMoveSpeed = 0.5F;
    float maxMoveSpeed = 200.0F;
};

// First-person free-fly camera movement (no collision or gravity): moves
// along the view direction, turns with yaw/pitch. Not a character controller.
class FreeFlyController {
public:
    explicit FreeFlyController(const FreeFlySettings& settings = {}) noexcept;

    // Turns first, then moves with the new orientation. A combined direction
    // is normalized, so moving diagonally is not faster. Non-finite or negative
    // deltaSeconds move nothing.
    void update(Camera& camera, const FlyInput& input, float deltaSeconds) noexcept;

    // Current base speed after wheel adjustments.
    [[nodiscard]] float moveSpeed() const noexcept { return moveSpeed_; }

private:
    FreeFlySettings settings_;
    float moveSpeed_;
};

// Default bindings: W/S or Up/Down forward/back, A/D or Left/Right sideways,
// E or Space up, Q or Ctrl down, Shift fast, wheel speed. Mouse movement turns
// the camera only while `mouseLook` is true (the caller decides, e.g. while
// the right mouse button is held).
[[nodiscard]] FlyInput readFlyInput(const platform::InputState& input, bool mouseLook) noexcept;

} // namespace engine::camera
