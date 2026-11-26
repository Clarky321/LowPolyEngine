#pragma once

// Euler angles for the inspector (M5.8): how a stored rotation (a unit
// quaternion, M4) is shown and edited as three angles in degrees. UI-free.
//
// The convention: rotation = Ry(y) * Rx(x) * Rz(z) with column vectors (the
// engine's convention, engine/core/Math.hpp), that is a roll about Z first,
// then a pitch about X, then a yaw about Y, each about the world's axis, as
// the camera turns: yaw about +Y, pitch about +X, looking along -Z.
//
// Shown: x in [-90, 90], y and z in (-180, 180]. A quaternion and its negation
// show the same angles. At a pitch of +-90 degrees (gimbal lock) yaw and roll
// turn about the same axis: the roll is shown as 0 and the yaw holds both.
// Values within kEulerSnapDegrees of 0 are shown as 0, and of +-180 as 180,
// so that a rotation read back from float never shows "-0.00" or "-180.00".

#include "engine/core/Math.hpp"

namespace editor {

inline constexpr float kEulerSnapDegrees = 1.0e-4F;

// The rotation of three angles in degrees (x pitch, y yaw, z roll), normalized.
[[nodiscard]] engine::math::Quat quatFromEulerDegrees(const engine::math::Vec3& degrees) noexcept;

// The angles a rotation is shown with. The quaternion need not be normalized;
// a zero or non-finite one gives no rotation.
[[nodiscard]] engine::math::Vec3 eulerDegreesFromQuat(const engine::math::Quat& rotation) noexcept;

} // namespace editor
