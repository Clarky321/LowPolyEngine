#include "editor/document/ComponentValue.hpp"

#include <format>
#include <string>
#include <type_traits>
#include <utility>

namespace editor {

namespace {

using engine::assets::AssetType;
using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::PrefabInstanceComponent;
using engine::scene::ScriptComponent;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;

// Calls f(std::type_identity<T>{}) with the component type T of `type`.
template <typename F>
decltype(auto) withType(ComponentType type, F&& f)
{
    switch (type) {
    case ComponentType::Name:
        return std::forward<F>(f)(std::type_identity<NameComponent>{});
    case ComponentType::Transform:
        return std::forward<F>(f)(std::type_identity<TransformComponent>{});
    case ComponentType::StaticMesh:
        return std::forward<F>(f)(std::type_identity<StaticMeshComponent>{});
    case ComponentType::Camera:
        return std::forward<F>(f)(std::type_identity<CameraComponent>{});
    case ComponentType::DirectionalLight:
        return std::forward<F>(f)(std::type_identity<DirectionalLightComponent>{});
    case ComponentType::Script:
        return std::forward<F>(f)(std::type_identity<ScriptComponent>{});
    case ComponentType::Parent:
        return std::forward<F>(f)(std::type_identity<ParentComponent>{});
    case ComponentType::PrefabInstance:
        break;
    }
    return std::forward<F>(f)(std::type_identity<PrefabInstanceComponent>{});
}

std::optional<engine::core::Error> checkAsset(const engine::assets::AssetRegistry& assets,
                                              engine::assets::AssetId id, AssetType expected,
                                              std::string_view where)
{
    if (auto error = assets.checkType(id, expected)) {
        return engine::core::Error{std::format("{}: {}", where, error->message)};
    }
    return std::nullopt;
}

// The asset references of a component; most components have none.
template <typename T>
std::optional<engine::core::Error> checkAssets(const T& /*value*/,
                                               const engine::assets::AssetRegistry& /*assets*/)
{
    return std::nullopt;
}

std::optional<engine::core::Error> checkAssets(const StaticMeshComponent& value,
                                               const engine::assets::AssetRegistry& assets)
{
    if (auto error = checkAsset(assets, value.mesh, AssetType::Mesh, "static mesh: mesh")) {
        return error;
    }
    if (!value.material.isNull()) {
        return checkAsset(assets, value.material, AssetType::Material, "static mesh: material");
    }
    return std::nullopt;
}

std::optional<engine::core::Error> checkAssets(const ScriptComponent& value,
                                               const engine::assets::AssetRegistry& assets)
{
    return checkAsset(assets, value.asset, AssetType::Script, "script: asset");
}

std::optional<engine::core::Error> checkAssets(const PrefabInstanceComponent& value,
                                               const engine::assets::AssetRegistry& assets)
{
    return checkAsset(assets, value.prefab, AssetType::Prefab, "prefab instance: prefab");
}

} // namespace

ComponentType typeOf(const ComponentValue& value) noexcept
{
    return static_cast<ComponentType>(value.index());
}

std::string_view typeName(ComponentType type) noexcept
{
    return withType(type, [](auto tag) -> std::string_view {
        using T = typename decltype(tag)::type;
        return T::kTypeName;
    });
}

bool hasComponent(const engine::scene::Scene& scene, engine::scene::EntityId entity,
                  ComponentType type)
{
    return withType(type, [&](auto tag) {
        using T = typename decltype(tag)::type;
        return scene.hasComponent<T>(entity);
    });
}

engine::core::Result<ComponentValue> componentOf(const engine::scene::Scene& scene,
                                                 engine::scene::EntityId entity, ComponentType type)
{
    return withType(type, [&](auto tag) -> engine::core::Result<ComponentValue> {
        using T = typename decltype(tag)::type;
        auto component = scene.component<T>(entity);
        if (!component) {
            return component.error();
        }
        return ComponentValue{std::move(component).value()};
    });
}

std::vector<ComponentValue> componentsOf(const engine::scene::Scene& scene,
                                         engine::scene::EntityId entity)
{
    std::vector<ComponentValue> components;
    for (const ComponentType type : kComponentTypes) {
        if (hasComponent(scene, entity, type)) {
            auto component = componentOf(scene, entity, type);
            if (component) {
                components.push_back(std::move(component).value());
            }
        }
    }
    return components;
}

std::optional<engine::core::Error> addComponent(engine::scene::Scene& scene,
                                                engine::scene::EntityId entity,
                                                const ComponentValue& value)
{
    return std::visit([&](const auto& component) { return scene.addComponent(entity, component); },
                      value);
}

std::optional<engine::core::Error> replaceComponent(engine::scene::Scene& scene,
                                                    engine::scene::EntityId entity,
                                                    const ComponentValue& value)
{
    return std::visit(
        [&](const auto& component) { return scene.replaceComponent(entity, component); }, value);
}

std::optional<engine::core::Error>
removeComponent(engine::scene::Scene& scene, engine::scene::EntityId entity, ComponentType type)
{
    return withType(type, [&](auto tag) {
        using T = typename decltype(tag)::type;
        return scene.removeComponent<T>(entity);
    });
}

std::optional<engine::core::Error> checkComponent(const ComponentValue& value,
                                                  const engine::assets::AssetRegistry& assets)
{
    return std::visit(
        [&](const auto& component) -> std::optional<engine::core::Error> {
            if (auto error = validate(component)) {
                return error;
            }
            return checkAssets(component, assets);
        },
        value);
}

} // namespace editor
