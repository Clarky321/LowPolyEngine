#pragma once

// The assets of one project: persistent asset IDs with their type and path
// (M4.3, ADR 0022). The registry knows where an asset's file is; it does not
// load files and holds no GPU resources: a caller reads the file it resolves
// with the engine's CPU loaders (geometry::readObj, core::readPng), and the
// renderer makes GPU resources from the result.
//
// The project root is given at runtime and never saved; paths are relative
// to it, so a moved project resolves the same assets. A registry is a value
// owned by its caller (no global registry) and not thread-safe.

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetPath.hpp"
#include "engine/assets/AssetType.hpp"
#include "engine/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets {

// Whether an asset's file was there at the last check. A missing file is a
// state, not an error: a project may list assets whose files are not there
// yet (or no longer); resolving such an asset for loading fails.
enum class AssetAvailability : std::uint8_t {
    Present, // a regular file with exactly the path's spelling
    Missing, // no such file, or only with a different letter case
};

struct AssetMetadata {
    AssetId id;
    AssetType type;
    AssetPath path;
    AssetAvailability availability = AssetAvailability::Missing;

    // The asset's name in messages: its file name, e.g. "tree.obj".
    [[nodiscard]] std::string_view sourceName() const noexcept { return path.fileName(); }
};

class AssetRegistry {
public:
    explicit AssetRegistry(std::filesystem::path projectRoot);

    [[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return root_; }

    // Registers an asset and checks whether its file exists. Fails, changing
    // nothing, for the null ID, an ID or a path already registered, a path or
    // folder that differs from a registered one only in letter case (one file
    // on Windows, two on Linux), a path that is a registered asset's folder or
    // has a registered file as a folder, and an extension the type does not
    // allow (assetExtensions). The errors name both assets.
    [[nodiscard]] std::optional<core::Error> registerAsset(AssetId id, AssetType type,
                                                           const AssetPath& path);
    // Fails for an ID that is not registered.
    [[nodiscard]] std::optional<core::Error> unregisterAsset(AssetId id);

    [[nodiscard]] bool contains(AssetId id) const noexcept;
    // A copy of the asset's metadata; fails for an ID that is not registered.
    [[nodiscard]] core::Result<AssetMetadata> find(AssetId id) const;
    // The asset registered with exactly this path, if any.
    [[nodiscard]] std::optional<AssetId> findByPath(const AssetPath& path) const;
    // Nothing if the asset is registered with the expected type, otherwise the
    // error, e.g. "asset …: 'crate.png' is a texture, expected a mesh".
    [[nodiscard]] std::optional<core::Error> checkType(AssetId id, AssetType expected) const;
    // The file of an asset of the expected type, checked on disk now: fails for
    // an ID that is not registered, another type, or a missing file (naming
    // the spelling found on disk if only the letter case differs).
    [[nodiscard]] core::Result<std::filesystem::path> resolve(AssetId id, AssetType expected) const;

    // Checks every asset's file again; returns the missing ones in path order.
    std::vector<AssetId> refreshAvailability();

    // All assets in path order (paths compared in folded case).
    [[nodiscard]] std::vector<AssetMetadata> assets() const;
    [[nodiscard]] std::size_t size() const noexcept { return byId_.size(); }

private:
    struct Folder {
        std::string spelling;
        std::size_t uses = 0;
    };

    [[nodiscard]] std::optional<core::Error> checkPath(AssetId id, const AssetPath& path) const;
    [[nodiscard]] core::Error unknown(AssetId id) const;

    std::filesystem::path root_;
    std::map<AssetId, AssetMetadata> byId_;
    // Folded path -> asset; folded folder -> its spelling and number of assets.
    std::map<std::string, AssetId> byFoldedPath_;
    std::map<std::string, Folder> folders_;
};

} // namespace engine::assets
