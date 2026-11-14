#pragma once

// A mesh drawn at the entity's transform (M4.3, ADR 0022): the asset IDs of a
// mesh and of a material, never GPU handles. Turning them into renderer
// resources is later work (M5); findAssetReferenceProblems
// (engine/scene/AssetReferences.hpp) checks them against an asset registry.

#include "engine/assets/AssetId.hpp"
#include "engine/core/Result.hpp"

#include <optional>
#include <string_view>

namespace engine::scene {

struct StaticMeshComponent {
    static constexpr std::string_view kTypeName = "static mesh";

    // A mesh asset; never null.
    assets::AssetId mesh;
    // A material asset, or null for the renderer's default material (white
    // base color: the vertex colors as they are).
    assets::AssetId material;

    friend bool operator==(const StaticMeshComponent&, const StaticMeshComponent&) = default;
};

// The mesh must not be null; the error names the field. Whether the IDs are
// registered assets of the right type is the registry's question, not this
// one's. GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const StaticMeshComponent& component);

} // namespace engine::scene
