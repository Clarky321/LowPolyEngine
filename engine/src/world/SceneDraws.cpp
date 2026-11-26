#include "engine/world/SceneDraws.hpp"

#include "engine/scene/DirectionalLightComponent.hpp"
#include "engine/scene/StaticMeshComponent.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::world {

math::Mat4 modelMatrix(const scene::TransformComponent& transform)
{
    const math::Mat4 translation = glm::translate(math::Mat4{1.0F}, transform.position);
    return glm::scale(translation * glm::mat4_cast(transform.rotation), transform.scale);
}

std::vector<MeshDraw> meshDraws(const scene::Scene& scene)
{
    std::vector<MeshDraw> draws;
    for (const scene::EntityId entity : scene.entitiesWith<scene::StaticMeshComponent>()) {
        const auto mesh = scene.component<scene::StaticMeshComponent>(entity);
        if (!mesh) {
            continue;
        }
        const auto transform = scene.component<scene::TransformComponent>(entity);
        draws.push_back({.entity = entity,
                         .mesh = mesh.value().mesh,
                         .model = transform ? modelMatrix(transform.value()) : math::Mat4{1.0F}});
    }
    return draws;
}

render::LightDesc sceneLight(const scene::Scene& scene, const render::LightDesc& fallback)
{
    const std::vector<scene::EntityId> lights =
        scene.entitiesWith<scene::DirectionalLightComponent>();
    if (lights.empty()) {
        return fallback;
    }
    const auto light = scene.component<scene::DirectionalLightComponent>(lights.front());
    if (!light) {
        return fallback;
    }
    const scene::DirectionalLightComponent& sun = light.value();
    return render::LightDesc{.direction = sun.direction,
                             .color = sun.color * sun.intensity,
                             .ambient = fallback.ambient};
}

} // namespace engine::world
