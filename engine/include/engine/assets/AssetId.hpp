#pragma once

// The persistent identity of an asset (M4.3, ADR 0020, ADR 0022): what scenes,
// prefabs and the asset manifest store to refer to a mesh, texture, material,
// script or prefab. A distinct type: it does not convert to or from entity
// IDs, scene IDs, integers, pointers or renderer handles (MeshHandle,
// TextureHandle, MaterialHandle), which are runtime resources of one
// renderer. The null AssetId refers to no asset.

#include "engine/core/Uuid.hpp"

#include <string_view>

namespace engine::assets {

struct AssetIdTag {
    static constexpr std::string_view kName = "asset ID";
};

using AssetId = core::TypedUuid<AssetIdTag>;

} // namespace engine::assets
