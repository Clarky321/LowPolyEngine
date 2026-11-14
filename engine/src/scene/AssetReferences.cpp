#include "engine/scene/AssetReferences.hpp"

#include <format>

namespace engine::scene {

namespace {

void check(const Scene& scene, const assets::AssetRegistry& registry, EntityId entity,
           std::string_view component, std::string_view field, assets::AssetId asset,
           assets::AssetType expected, std::vector<AssetReferenceProblem>& problems)
{
    if (auto error = registry.checkType(asset, expected)) {
        problems.push_back(AssetReferenceProblem{
            entity, component, field, asset,
            std::format("scene '{}': entity {}: {} component: {}: {}", scene.name(),
                        entity.toString(), component, field, error->message)});
    }
}

} // namespace

std::vector<AssetReferenceProblem> findAssetReferenceProblems(const Scene& scene,
                                                              const assets::AssetRegistry& registry)
{
    constexpr std::string_view kMesh = StaticMeshComponent::kTypeName;
    constexpr std::string_view kScript = ScriptComponent::kTypeName;
    std::vector<AssetReferenceProblem> problems;
    for (const EntityId entity : scene.entityIds()) {
        if (scene.hasComponent<StaticMeshComponent>(entity)) {
            const StaticMeshComponent mesh = scene.component<StaticMeshComponent>(entity).value();
            // Never null in a scene (validate), so checked always.
            check(scene, registry, entity, kMesh, "mesh", mesh.mesh, assets::AssetType::Mesh,
                  problems);
            if (!mesh.material.isNull()) {
                check(scene, registry, entity, kMesh, "material", mesh.material,
                      assets::AssetType::Material, problems);
            }
        }
        if (scene.hasComponent<ScriptComponent>(entity)) {
            const ScriptComponent script = scene.component<ScriptComponent>(entity).value();
            check(scene, registry, entity, kScript, "asset", script.asset,
                  assets::AssetType::Script, problems);
        }
    }
    return problems;
}

} // namespace engine::scene
