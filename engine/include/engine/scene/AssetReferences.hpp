#pragma once

// Checks a scene's asset references against an asset registry (M4.3, ADR
// 0022): a scene stores asset IDs only, so whether they name registered assets
// of the right type is checked against the registry of the project the scene
// belongs to. Missing files are not reported here; they are the registry's
// availability.

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/Scene.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace engine::scene {

struct AssetReferenceProblem {
    EntityId entity;
    // The component type and field, e.g. "static mesh" and "material".
    std::string_view component;
    std::string_view field;
    assets::AssetId asset;
    // e.g. "scene 'Main': entity …: static mesh component: material: asset …:
    // 'crate.png' is a texture, expected a material".
    std::string message;
};

// Every reference that names an asset the registry does not know or an asset
// of another type, in the entities' creation order. References that may be
// null (a static mesh's material) and are null are not problems.
[[nodiscard]] std::vector<AssetReferenceProblem>
findAssetReferenceProblems(const Scene& scene, const assets::AssetRegistry& registry);

} // namespace engine::scene
