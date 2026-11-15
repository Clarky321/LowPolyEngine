#pragma once

// A perspective camera's lens (M4.2, ADR 0021). Its position and orientation
// come from the entity's TransformComponent; the camera looks along the
// transform's -Z with +Y up, like engine::camera::Camera.

#include "engine/core/Result.hpp"

#include <optional>
#include <string_view>

namespace engine::scene {

struct CameraComponent {
    static constexpr std::string_view kTypeName = "camera";

    // The ranges camera::projectionMatrix uses unchanged: inside them it does
    // not clamp or replace a value.
    static constexpr float kMinVerticalFovRadians = 0.0174533F; // 1 degree
    static constexpr float kMaxVerticalFovRadians = 3.1241393F; // 179 degrees
    static constexpr float kMinNearPlane = 0.001F;
    // The far plane lies beyond nearPlane * kMinFarToNearRatio.
    static constexpr float kMinFarToNearRatio = 1.001F;

    float verticalFovRadians = 1.0471976F; // 60 degrees
    float nearPlane = 0.1F;
    float farPlane = 1000.0F;

    friend bool operator==(const CameraComponent&, const CameraComponent&) = default;
};

// Every value finite; the field of view in [kMinVerticalFovRadians,
// kMaxVerticalFovRadians]; nearPlane >= kMinNearPlane; farPlane >
// nearPlane * kMinFarToNearRatio. The error names the first offending field,
// e.g. "camera component: nearPlane is 0, must be at least 0.001". GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const CameraComponent& component);

} // namespace engine::scene
