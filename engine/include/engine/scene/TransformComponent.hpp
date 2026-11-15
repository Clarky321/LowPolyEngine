#pragma once

// Where an entity is, how it is turned and how large it is (M4.2, ADR 0021).
// World space, the conventions of engine/core/Math.hpp. There is no parent
// transform in M4: every transform is relative to the world.

#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"

#include <optional>
#include <string_view>

namespace engine::scene {

struct TransformComponent {
    static constexpr std::string_view kTypeName = "transform";

    // Largest absolute value of a position coordinate (100 km at one unit per
    // metre; floats there are still 8 mm apart).
    static constexpr float kMaxCoordinate = 1.0e5F;
    // A rotation is accepted when its length differs from 1 by at most this;
    // a scene stores it normalized.
    static constexpr float kRotationLengthTolerance = 1.0e-3F;
    // Range of each scale component. Zero and negative scales are rejected:
    // a negative scale would flip the triangle winding, which the renderer
    // culls (mirroring needs its own decision).
    static constexpr float kMinScale = 1.0e-4F;
    static constexpr float kMaxScale = 1.0e4F;

    math::Vec3 position{0.0F};
    math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F}; // (w, x, y, z): no rotation
    math::Vec3 scale{1.0F};

    friend bool operator==(const TransformComponent&, const TransformComponent&) = default;
};

// Every value finite; each position coordinate within +-kMaxCoordinate; the
// rotation's length within kRotationLengthTolerance of 1; each scale
// component in [kMinScale, kMaxScale]. The error names the first offending
// field, e.g. "transform component: scale.y is 0, must be in [0.0001, 10000]".
// GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const TransformComponent& component);

} // namespace engine::scene
