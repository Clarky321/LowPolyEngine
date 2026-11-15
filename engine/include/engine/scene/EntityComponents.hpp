#pragma once

// The components of one entity that are plain values (M4.6, ADR 0025): every
// component type except the links that name other entities or prefabs
// (ParentComponent, PrefabInstanceComponent), whose meaning depends on where
// the entity is. Copying an entity between a scene and a prefab copies these
// and maps the links separately.

#include "engine/core/Result.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DirectionalLightComponent.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ScriptComponent.hpp"
#include "engine/scene/StaticMeshComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <optional>

namespace engine::scene {

class Scene;

struct EntityComponents {
    std::optional<NameComponent> name;
    std::optional<TransformComponent> transform;
    std::optional<StaticMeshComponent> staticMesh;
    std::optional<CameraComponent> camera;
    std::optional<DirectionalLightComponent> directionalLight;
    std::optional<ScriptComponent> script;

    friend bool operator==(const EntityComponents&, const EntityComponents&) = default;
};

// The first component that fails its validate, with the error of that
// validate. GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const EntityComponents& components);

// Copies of the entity's plain components; empty for an entity the scene does
// not contain.
[[nodiscard]] EntityComponents componentsOf(const Scene& scene, EntityId entity);

// Adds every component present (Scene::addComponent each). Stops at the first
// error; the components added before it stay, so callers that need all or
// nothing destroy the entity on failure.
[[nodiscard]] std::optional<core::Error> addComponents(Scene& scene, EntityId entity,
                                                       const EntityComponents& components);

} // namespace engine::scene
