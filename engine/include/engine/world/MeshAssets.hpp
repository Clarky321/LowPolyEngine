#pragma once

// Mesh assets read for drawing (M5.6): a cache by asset ID of the meshes a
// scene's entities use. Each asset is resolved through the registry as a mesh
// and read by the OBJ loader (ADR 0012) the first time it is asked for, and
// never again while it is cached: neither its mesh nor its failure. A failure
// (an asset that is not registered, is of another type, has no file, or has a
// broken file) is kept with its message, so it is reported once. CPU only, so
// it is tested headless; SceneView uploads the meshes.
//
// The cache belongs to one project's registry: an asset ID names one file
// only within it, so the cache is cleared for another project.

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/geometry/MeshData.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace engine::world {

// A mesh asset as it was read: its mesh (geometry::validate() passed), or why
// it could not be read.
struct MeshAsset {
    std::optional<geometry::MeshData> mesh;
    // Empty when the mesh was read, e.g. "asset …: 'crate.png' is a texture,
    // expected a mesh" or "asset … (mesh 'models/tower.obj'): line 6: …".
    std::string problem;
};

class MeshAssets {
public:
    // Reads every asset of `meshes` that is not cached yet (each once, also
    // when it is listed several times). Returns the problems of the assets
    // read by this call that could not be read, in the order of `meshes`;
    // assets cached earlier add none.
    std::vector<std::string> load(const std::vector<assets::AssetId>& meshes,
                                  const assets::AssetRegistry& registry);

    // The cached asset; null if it was never asked for.
    [[nodiscard]] const MeshAsset* find(assets::AssetId mesh) const;

    // Assets cached, read or not.
    [[nodiscard]] std::size_t size() const noexcept { return assets_.size(); }

    // Forgets every asset (another project, or a fresh start).
    void clear() noexcept { assets_.clear(); }

private:
    std::map<assets::AssetId, MeshAsset> assets_;
};

} // namespace engine::world
