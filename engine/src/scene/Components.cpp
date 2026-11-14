#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DirectionalLightComponent.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ParentComponent.hpp"
#include "engine/scene/PrefabInstanceComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/ScriptComponent.hpp"
#include "engine/scene/StaticMeshComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <string>
#include <system_error>

namespace engine::scene {

namespace {

// Numbers in messages: plain notation where six significant digits give the
// value back exactly (0.0001, 100000), otherwise the shortest exact form
// (100000.01, 3.1241393), so a message never shows a value that is not the
// one checked.
template <typename T>
std::string text(T value)
{
    std::string plain = std::format("{:g}", value);
    T parsed{};
    const auto [end, error] = std::from_chars(plain.data(), plain.data() + plain.size(), parsed);
    if (!std::isfinite(value) ||
        (error == std::errc{} && end == plain.data() + plain.size() && parsed == value)) {
        return plain;
    }
    return std::format("{}", value);
}

core::Error fieldError(std::string_view component, std::string_view field, float value,
                       std::string_view rule)
{
    return core::Error{
        std::format("{} component: {} is {}, {}", component, field, text(value), rule)};
}

std::optional<core::Error> checkFinite(std::string_view component, std::string_view field,
                                       float value)
{
    if (!std::isfinite(value)) {
        return fieldError(component, field, value, "must be finite");
    }
    return std::nullopt;
}

std::optional<core::Error> checkRange(std::string_view component, std::string_view field,
                                      float value, float low, float high)
{
    if (!std::isfinite(value)) {
        return fieldError(component, field, value, "must be finite");
    }
    if (value < low || value > high) {
        return fieldError(component, field, value,
                          std::format("must be in [{}, {}]", text(low), text(high)));
    }
    return std::nullopt;
}

std::optional<core::Error> checkAtLeast(std::string_view component, std::string_view field,
                                        float value, float low)
{
    if (!std::isfinite(value)) {
        return fieldError(component, field, value, "must be finite");
    }
    if (value < low) {
        return fieldError(component, field, value, std::format("must be at least {}", text(low)));
    }
    return std::nullopt;
}

} // namespace

std::optional<core::Error> validate(const NameComponent& component)
{
    return validateName(component.value, "name component: value");
}

std::optional<core::Error> validate(const TransformComponent& component)
{
    constexpr std::string_view kType = TransformComponent::kTypeName;
    constexpr float kMax = TransformComponent::kMaxCoordinate;
    const math::Vec3& position = component.position;
    if (auto error = checkRange(kType, "position.x", position.x, -kMax, kMax)) {
        return error;
    }
    if (auto error = checkRange(kType, "position.y", position.y, -kMax, kMax)) {
        return error;
    }
    if (auto error = checkRange(kType, "position.z", position.z, -kMax, kMax)) {
        return error;
    }

    const math::Quat& rotation = component.rotation;
    if (auto error = checkFinite(kType, "rotation.w", rotation.w)) {
        return error;
    }
    if (auto error = checkFinite(kType, "rotation.x", rotation.x)) {
        return error;
    }
    if (auto error = checkFinite(kType, "rotation.y", rotation.y)) {
        return error;
    }
    if (auto error = checkFinite(kType, "rotation.z", rotation.z)) {
        return error;
    }
    // In double: the squares of large finite floats do not overflow there.
    const auto w = static_cast<double>(rotation.w);
    const auto x = static_cast<double>(rotation.x);
    const auto y = static_cast<double>(rotation.y);
    const auto z = static_cast<double>(rotation.z);
    const double length = std::sqrt((w * w) + (x * x) + (y * y) + (z * z));
    if (std::abs(length - 1.0) >
        static_cast<double>(TransformComponent::kRotationLengthTolerance)) {
        return core::Error{std::format("{} component: rotation has length {}, must be within {} "
                                       "of 1",
                                       kType, text(length),
                                       text(TransformComponent::kRotationLengthTolerance))};
    }

    constexpr float kMinScale = TransformComponent::kMinScale;
    constexpr float kMaxScale = TransformComponent::kMaxScale;
    const math::Vec3& scale = component.scale;
    if (auto error = checkRange(kType, "scale.x", scale.x, kMinScale, kMaxScale)) {
        return error;
    }
    if (auto error = checkRange(kType, "scale.y", scale.y, kMinScale, kMaxScale)) {
        return error;
    }
    return checkRange(kType, "scale.z", scale.z, kMinScale, kMaxScale);
}

std::optional<core::Error> validate(const CameraComponent& component)
{
    constexpr std::string_view kType = CameraComponent::kTypeName;
    if (auto error = checkRange(kType, "verticalFovRadians", component.verticalFovRadians,
                                CameraComponent::kMinVerticalFovRadians,
                                CameraComponent::kMaxVerticalFovRadians)) {
        return error;
    }
    if (auto error =
            checkAtLeast(kType, "nearPlane", component.nearPlane, CameraComponent::kMinNearPlane)) {
        return error;
    }
    if (auto error = checkFinite(kType, "farPlane", component.farPlane)) {
        return error;
    }
    // The same float expression camera::projectionMatrix compares with.
    const float minFar = component.nearPlane * CameraComponent::kMinFarToNearRatio;
    if (!(component.farPlane > minFar)) {
        return fieldError(kType, "farPlane", component.farPlane,
                          std::format("must be greater than nearPlane * {} ({})",
                                      text(CameraComponent::kMinFarToNearRatio), text(minFar)));
    }
    return std::nullopt;
}

std::optional<core::Error> validate(const StaticMeshComponent& component)
{
    if (component.mesh.isNull()) {
        return core::Error{std::format("{} component: mesh is the null asset ID, must name a "
                                       "mesh asset",
                                       StaticMeshComponent::kTypeName)};
    }
    return std::nullopt;
}

std::optional<core::Error> validate(const ScriptComponent& component)
{
    if (component.asset.isNull()) {
        return core::Error{std::format("{} component: asset is the null asset ID, must name a "
                                       "script asset",
                                       ScriptComponent::kTypeName)};
    }
    return std::nullopt;
}

std::optional<core::Error> validate(const ParentComponent& component)
{
    if (component.entity.isNull()) {
        return core::Error{std::format("{} component: entity is the null entity ID, must name the "
                                       "parent",
                                       ParentComponent::kTypeName)};
    }
    return std::nullopt;
}

std::optional<core::Error> validate(const PrefabInstanceComponent& component)
{
    constexpr std::string_view kType = PrefabInstanceComponent::kTypeName;
    if (component.prefab.isNull()) {
        return core::Error{std::format(
            "{} component: prefab is the null asset ID, must name a prefab asset", kType)};
    }
    if (component.instance.isNull()) {
        return core::Error{std::format(
            "{} component: instance is the null prefab instance ID, must name the instance",
            kType)};
    }
    if (component.localId < 1) {
        return core::Error{std::format("{} component: localId is {}, must be at least 1", kType,
                                       component.localId)};
    }
    return std::nullopt;
}

std::optional<core::Error> validate(const DirectionalLightComponent& component)
{
    constexpr std::string_view kType = DirectionalLightComponent::kTypeName;
    const math::Vec3& direction = component.direction;
    if (auto error = checkFinite(kType, "direction.x", direction.x)) {
        return error;
    }
    if (auto error = checkFinite(kType, "direction.y", direction.y)) {
        return error;
    }
    if (auto error = checkFinite(kType, "direction.z", direction.z)) {
        return error;
    }
    if (direction.x == 0.0F && direction.y == 0.0F && direction.z == 0.0F) {
        return core::Error{
            std::format("{} component: direction is (0, 0, 0), must not be zero", kType)};
    }

    const math::Vec3& color = component.color;
    if (auto error = checkAtLeast(kType, "color.r", color.r, 0.0F)) {
        return error;
    }
    if (auto error = checkAtLeast(kType, "color.g", color.g, 0.0F)) {
        return error;
    }
    if (auto error = checkAtLeast(kType, "color.b", color.b, 0.0F)) {
        return error;
    }
    if (auto error = checkAtLeast(kType, "intensity", component.intensity, 0.0F)) {
        return error;
    }
    // The renderer gets color * intensity, which must stay finite too.
    const math::Vec3 radiance = color * component.intensity;
    if (!std::isfinite(radiance.r) || !std::isfinite(radiance.g) || !std::isfinite(radiance.b)) {
        return core::Error{std::format("{} component: color * intensity is ({}, {}, {}), must be "
                                       "finite",
                                       kType, text(radiance.r), text(radiance.g),
                                       text(radiance.b))};
    }
    return std::nullopt;
}

} // namespace engine::scene
