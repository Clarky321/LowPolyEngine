#pragma once

// What a scene asks the renderer to draw (M5.6): its entities with a static
// mesh, each at its own transform, and the light they are drawn in. Pure
// functions of the scene, without files or GPU, so they are tested headless;
// SceneView (SceneView.hpp) draws them.
//
// Transforms are not inherited: a parent link (ParentComponent) does not move
// its children (M4, ADR 0021), so every entity is placed by its own
// transform only.

#include "engine/assets/AssetId.hpp"
#include "engine/core/Math.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <vector>

namespace engine::world {

// The object-to-world matrix of a transform: scale, then rotation, then
// translation (T * R * S).
[[nodiscard]] math::Mat4 modelMatrix(const scene::TransformComponent& transform);

// One entity with a static mesh, as it is drawn.
struct MeshDraw {
    scene::EntityId entity;
    assets::AssetId mesh;
    // modelMatrix() of the entity's transform; the identity (the transform's
    // defaults) for an entity without one.
    math::Mat4 model{1.0F};
};

// The scene's entities with a StaticMeshComponent, in creation order. The
// component's material is not used: every mesh is drawn with the default
// material until material files exist (M6).
[[nodiscard]] std::vector<MeshDraw> meshDraws(const scene::Scene& scene);

// The light the scene's entities are drawn in: the first entity (in creation
// order) with a DirectionalLightComponent gives the direction and color *
// intensity, with the fallback's ambient, since the component has none.
// Without such an entity, the fallback (the world's sun).
[[nodiscard]] render::LightDesc sceneLight(const scene::Scene& scene,
                                           const render::LightDesc& fallback);

} // namespace engine::world
