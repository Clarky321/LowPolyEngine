#pragma once

// A directional light such as the sun (M4.2, ADR 0021). The renderer has one
// directional light (ADR 0011): it gets direction and color * intensity as
// render::LightDesc. Mapping a scene onto the renderer is later work (M5);
// no other kinds of light exist yet.

#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"

#include <optional>
#include <string_view>

namespace engine::scene {

struct DirectionalLightComponent {
    static constexpr std::string_view kTypeName = "directional light";

    // World-space direction the light travels in, as in render::LightDesc.
    // Any length except zero; a scene stores it normalized.
    math::Vec3 direction{0.0F, -1.0F, 0.0F};
    // RGB in display space, like vertex and material colors (ADR 0009).
    math::Vec3 color{1.0F};
    float intensity = 1.0F;

    friend bool operator==(const DirectionalLightComponent&,
                           const DirectionalLightComponent&) = default;
};

// Every value finite; the direction not zero; each color component and the
// intensity >= 0; color * intensity finite. The error names the first
// offending field, e.g. "directional light component: intensity is -1, must
// be at least 0". GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const DirectionalLightComponent& component);

} // namespace engine::scene
