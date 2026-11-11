// GPU-free parts of the material model (ADR 0010), so they can be unit-tested.

#include "engine/render/Renderer.hpp"

#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace engine::render {

std::optional<core::Error> validate(const MaterialDesc& desc)
{
    const Color& color = desc.baseColor;
    const std::array<std::pair<const char*, float>, 4> components{{
        {"red", color.r},
        {"green", color.g},
        {"blue", color.b},
        {"alpha", color.a},
    }};
    for (const auto& [name, value] : components) {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
            return core::Error{
                std::format("material base color {} is {}; components must be finite and in [0, 1]",
                            name, value)};
        }
    }
    return std::nullopt;
}

} // namespace engine::render
