#include "engine/scene/AssetReferences.hpp"

#include <format>

namespace engine::scene {

namespace {

void check(const Scene& scene, const assets::AssetRegistry& registry, EntityId entity,
           std::string_view field, assets::AssetId asset, assets::AssetType expected,
           std::vector<AssetReferenceProblem>& problems)
{
    if (auto error = registry.checkType(asset, expected)) {
        problems.push_back(AssetReferenceProblem{
            entity, StaticMeshComponent::kTypeName, field, asset,
            std::format("scene '{}': entity {}: {} component: {}: {}", scene.name(),
                        entity.toString(), StaticMeshComponent::kTypeName, field, error->message)});
    }
}

} // namespace

std::vector<AssetReferenceProblem> findAssetReferenceProblems(const Scene& scene,
                                                              const assets::AssetRegistry& registry)
{
    std::vector<AssetReferenceProblem> problems;
    for (const EntityId entity : scene.entitiesWith<StaticMeshComponent>()) {
        const StaticMeshComponent mesh = scene.component<StaticMeshComponent>(entity).value();
        // Never null in a scene (validate), so checked always.
        check(scene, registry, entity, "mesh", mesh.mesh, assets::AssetType::Mesh, problems);
        if (!mesh.material.isNull()) {
            check(scene, registry, entity, "material", mesh.material, assets::AssetType::Material,
                  problems);
        }
    }
    return problems;
}

} // namespace engine::scene
