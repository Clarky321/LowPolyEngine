#include "engine/world/MeshAssets.hpp"

#include "engine/assets/AssetType.hpp"
#include "engine/geometry/ObjImport.hpp"

#include <format>
#include <utility>

namespace engine::world {

namespace {

MeshAsset read(assets::AssetId mesh, const assets::AssetRegistry& registry)
{
    const auto file = registry.resolve(mesh, assets::AssetType::Mesh);
    if (!file) {
        return MeshAsset{.mesh = std::nullopt, .problem = file.error().message};
    }
    auto obj = geometry::readObj(file.value());
    if (!obj) {
        // resolve() succeeded, so the asset is registered.
        const auto metadata = registry.find(mesh);
        const std::string path = metadata ? std::string(metadata.value().path.text()) : "";
        return MeshAsset{.mesh = std::nullopt,
                         .problem = std::format("asset {} (mesh '{}'): {}", mesh.toString(), path,
                                                obj.error().message)};
    }
    return MeshAsset{.mesh = std::move(obj).value().mesh, .problem = {}};
}

} // namespace

std::vector<std::string> MeshAssets::load(const std::vector<assets::AssetId>& meshes,
                                          const assets::AssetRegistry& registry)
{
    std::vector<std::string> problems;
    for (const assets::AssetId mesh : meshes) {
        if (assets_.contains(mesh)) {
            continue;
        }
        const auto [entry, inserted] = assets_.emplace(mesh, read(mesh, registry));
        if (inserted && !entry->second.mesh) {
            problems.push_back(entry->second.problem);
        }
    }
    return problems;
}

const MeshAsset* MeshAssets::find(assets::AssetId mesh) const
{
    const auto found = assets_.find(mesh);
    return found == assets_.end() ? nullptr : &found->second;
}

} // namespace engine::world
