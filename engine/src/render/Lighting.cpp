// GPU-free parts of the lighting model (ADR 0011), so they can be unit-tested.

#include "Lighting.hpp"

#include "engine/render/Renderer.hpp"

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace engine::render {

namespace {

bool isFinite(const math::Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Each component finite and >= 0; the error names the first offending one.
std::optional<core::Error> checkColor(const char* name, const math::Vec3& color)
{
    const std::array<std::pair<const char*, float>, 3> components{{
        {"red", color.r},
        {"green", color.g},
        {"blue", color.b},
    }};
    for (const auto& [component, value] : components) {
        if (!std::isfinite(value) || value < 0.0F) {
            return core::Error{std::format("light {} {} is {}; components must be finite and >= 0",
                                           name, component, value)};
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<core::Error> validate(const LightDesc& desc)
{
    // dot > 0 also rules out directions so short that their squared length
    // underflows, which could not be normalized.
    const math::Vec3& direction = desc.direction;
    if (!isFinite(direction) || glm::dot(direction, direction) <= 0.0F) {
        return core::Error{std::format("light direction is ({}, {}, {}); it must be finite and "
                                       "not zero",
                                       direction.x, direction.y, direction.z)};
    }
    if (auto error = checkColor("color", desc.color)) {
        return error;
    }
    return checkColor("ambient", desc.ambient);
}

glm::mat3 normalMatrix(const math::Mat4& model)
{
    const math::Vec3 x{model[0]};
    const math::Vec3 y{model[1]};
    const math::Vec3 z{model[2]};
    // Columns of the cofactor matrix: determinant x inverse transpose.
    glm::mat3 cofactors{glm::cross(y, z), glm::cross(z, x), glm::cross(x, y)};
    if (glm::dot(x, glm::cross(y, z)) < 0.0F) {
        cofactors = -cofactors;
    }
    return cofactors;
}

} // namespace engine::render
