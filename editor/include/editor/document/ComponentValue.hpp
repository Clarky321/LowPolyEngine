#pragma once

// A scene component of any type as one value (M5.5): what commands store to
// put a component back exactly, and what "create an entity with these
// components" takes. The operations call the scene's own, so every rule of
// the scene (validation, parent links without cycles) applies unchanged.

#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Result.hpp"
#include "engine/scene/Scene.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace editor {

// In the order the scene document writes components (ADR 0023).
using ComponentValue =
    std::variant<engine::scene::NameComponent, engine::scene::TransformComponent,
                 engine::scene::StaticMeshComponent, engine::scene::CameraComponent,
                 engine::scene::DirectionalLightComponent, engine::scene::ScriptComponent,
                 engine::scene::ParentComponent, engine::scene::PrefabInstanceComponent>;

// The alternatives of ComponentValue, in the same order.
enum class ComponentType : std::uint8_t {
    Name,
    Transform,
    StaticMesh,
    Camera,
    DirectionalLight,
    Script,
    Parent,
    PrefabInstance,
};

inline constexpr std::array<ComponentType, 8> kComponentTypes{
    ComponentType::Name,   ComponentType::Transform,        ComponentType::StaticMesh,
    ComponentType::Camera, ComponentType::DirectionalLight, ComponentType::Script,
    ComponentType::Parent, ComponentType::PrefabInstance,
};

[[nodiscard]] ComponentType typeOf(const ComponentValue& value) noexcept;
// The component type's name in messages and documents, e.g. "static mesh".
[[nodiscard]] std::string_view typeName(ComponentType type) noexcept;

[[nodiscard]] bool hasComponent(const engine::scene::Scene& scene, engine::scene::EntityId entity,
                                ComponentType type);
// A copy of the entity's component of the type; fails if it has none.
[[nodiscard]] engine::core::Result<ComponentValue>
componentOf(const engine::scene::Scene& scene, engine::scene::EntityId entity, ComponentType type);
// Every component of the entity, in the order of kComponentTypes.
[[nodiscard]] std::vector<ComponentValue> componentsOf(const engine::scene::Scene& scene,
                                                       engine::scene::EntityId entity);

// Scene::addComponent, replaceComponent and removeComponent for any type.
[[nodiscard]] std::optional<engine::core::Error> addComponent(engine::scene::Scene& scene,
                                                              engine::scene::EntityId entity,
                                                              const ComponentValue& value);
[[nodiscard]] std::optional<engine::core::Error> replaceComponent(engine::scene::Scene& scene,
                                                                  engine::scene::EntityId entity,
                                                                  const ComponentValue& value);
[[nodiscard]] std::optional<engine::core::Error>
removeComponent(engine::scene::Scene& scene, engine::scene::EntityId entity, ComponentType type);

// Nothing if the value passes its type's validate and every asset it names is
// registered with the type it needs (a static mesh's mesh and material, a
// script's script, a prefab instance's prefab); otherwise the first problem,
// starting with the component type, e.g. "static mesh: mesh: asset ...".
[[nodiscard]] std::optional<engine::core::Error>
checkComponent(const ComponentValue& value, const engine::assets::AssetRegistry& assets);

} // namespace editor
