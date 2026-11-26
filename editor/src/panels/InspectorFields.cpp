#include "editor/panels/InspectorFields.hpp"

#include "editor/panels/EulerAngles.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DirectionalLightComponent.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ScriptComponent.hpp"
#include "engine/scene/StaticMeshComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <glm/trigonometric.hpp>

#include <string>
#include <utility>
#include <variant>

namespace editor {

using engine::math::Vec3;
using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::TransformComponent;

namespace {

FieldValues fromVec3(const Vec3& v)
{
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

Vec3 toVec3(const FieldValues& values)
{
    return Vec3{static_cast<float>(values[0]), static_cast<float>(values[1]),
                static_cast<float>(values[2])};
}

FieldValues single(float value)
{
    return {static_cast<double>(value), 0.0, 0.0};
}

} // namespace

ComponentType componentTypeOf(FieldGroup group) noexcept
{
    switch (group) {
    case FieldGroup::Position:
    case FieldGroup::Rotation:
    case FieldGroup::Scale:
        return ComponentType::Transform;
    case FieldGroup::VerticalFov:
    case FieldGroup::NearPlane:
    case FieldGroup::FarPlane:
        return ComponentType::Camera;
    case FieldGroup::Direction:
    case FieldGroup::Color:
    case FieldGroup::Intensity:
        return ComponentType::DirectionalLight;
    }
    return ComponentType::Transform;
}

std::size_t fieldCount(FieldGroup group) noexcept
{
    switch (group) {
    case FieldGroup::VerticalFov:
    case FieldGroup::NearPlane:
    case FieldGroup::FarPlane:
    case FieldGroup::Intensity:
        return 1;
    case FieldGroup::Position:
    case FieldGroup::Rotation:
    case FieldGroup::Scale:
    case FieldGroup::Direction:
    case FieldGroup::Color:
        return 3;
    }
    return 3;
}

FieldValues fieldValues(const ComponentValue& component, FieldGroup group)
{
    if (const auto* transform = std::get_if<TransformComponent>(&component)) {
        switch (group) {
        case FieldGroup::Position:
            return fromVec3(transform->position);
        case FieldGroup::Rotation:
            return fromVec3(eulerDegreesFromQuat(transform->rotation));
        case FieldGroup::Scale:
            return fromVec3(transform->scale);
        default:
            break;
        }
    }
    if (const auto* camera = std::get_if<CameraComponent>(&component)) {
        switch (group) {
        case FieldGroup::VerticalFov:
            return single(glm::degrees(camera->verticalFovRadians));
        case FieldGroup::NearPlane:
            return single(camera->nearPlane);
        case FieldGroup::FarPlane:
            return single(camera->farPlane);
        default:
            break;
        }
    }
    if (const auto* light = std::get_if<DirectionalLightComponent>(&component)) {
        switch (group) {
        case FieldGroup::Direction:
            return fromVec3(light->direction);
        case FieldGroup::Color:
            return fromVec3(light->color);
        case FieldGroup::Intensity:
            return single(light->intensity);
        default:
            break;
        }
    }
    return {0.0, 0.0, 0.0};
}

ComponentValue withFieldValues(ComponentValue component, FieldGroup group,
                               const FieldValues& values)
{
    const auto first = static_cast<float>(values[0]);
    if (auto* transform = std::get_if<TransformComponent>(&component)) {
        switch (group) {
        case FieldGroup::Position:
            transform->position = toVec3(values);
            break;
        case FieldGroup::Rotation:
            transform->rotation = quatFromEulerDegrees(toVec3(values));
            break;
        case FieldGroup::Scale:
            transform->scale = toVec3(values);
            break;
        default:
            break;
        }
    } else if (auto* camera = std::get_if<CameraComponent>(&component)) {
        switch (group) {
        case FieldGroup::VerticalFov:
            camera->verticalFovRadians = glm::radians(first);
            break;
        case FieldGroup::NearPlane:
            camera->nearPlane = first;
            break;
        case FieldGroup::FarPlane:
            camera->farPlane = first;
            break;
        default:
            break;
        }
    } else if (auto* light = std::get_if<DirectionalLightComponent>(&component)) {
        switch (group) {
        case FieldGroup::Direction:
            light->direction = toVec3(values);
            break;
        case FieldGroup::Color:
            light->color = toVec3(values);
            break;
        case FieldGroup::Intensity:
            light->intensity = first;
            break;
        default:
            break;
        }
    }
    return component;
}

std::vector<AssetChoice> assetChoices(const engine::assets::AssetRegistry& registry,
                                      engine::assets::AssetType type)
{
    std::vector<AssetChoice> choices;
    for (const engine::assets::AssetMetadata& asset : registry.assets()) {
        if (asset.type == type) {
            choices.push_back({asset.id, asset.path.text()});
        }
    }
    return choices;
}

std::vector<ComponentType> addableComponents(const engine::scene::Scene& scene,
                                             engine::scene::EntityId entity)
{
    std::vector<ComponentType> types;
    if (!scene.contains(entity)) {
        return types;
    }
    for (const ComponentType type : kEditableComponents) {
        if (!hasComponent(scene, entity, type)) {
            types.push_back(type);
        }
    }
    return types;
}

engine::core::Result<ComponentValue> newComponent(ComponentType type,
                                                  const engine::assets::AssetRegistry& registry)
{
    using engine::assets::AssetType;
    switch (type) {
    case ComponentType::Name:
        return ComponentValue{engine::scene::NameComponent{std::string(kNewComponentName)}};
    case ComponentType::Transform:
        return ComponentValue{TransformComponent{}};
    case ComponentType::Camera:
        return ComponentValue{CameraComponent{}};
    case ComponentType::DirectionalLight:
        return ComponentValue{DirectionalLightComponent{}};
    case ComponentType::StaticMesh: {
        const std::vector<AssetChoice> meshes = assetChoices(registry, AssetType::Mesh);
        if (meshes.empty()) {
            return engine::core::Error{
                "a static mesh needs a mesh asset, and the project has none"};
        }
        return ComponentValue{engine::scene::StaticMeshComponent{meshes.front().id, {}}};
    }
    case ComponentType::Script: {
        const std::vector<AssetChoice> scripts = assetChoices(registry, AssetType::Script);
        if (scripts.empty()) {
            return engine::core::Error{"a script component needs a script asset, and the project "
                                       "has none"};
        }
        return ComponentValue{engine::scene::ScriptComponent{scripts.front().id, true}};
    }
    case ComponentType::Parent:
    case ComponentType::PrefabInstance:
        break;
    }
    return engine::core::Error{std::string("the inspector does not add a ") +
                               std::string(typeName(type)) + " component"};
}

} // namespace editor
