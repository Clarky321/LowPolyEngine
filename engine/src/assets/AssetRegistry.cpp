#include "engine/assets/AssetRegistry.hpp"

#include "engine/core/FileSystem.hpp"

#include <format>
#include <system_error>
#include <utility>

namespace engine::assets {

namespace {

std::vector<std::string_view> segmentsOf(std::string_view path)
{
    std::vector<std::string_view> segments;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        segments.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    return segments;
}

// The folders of a path, shortest first: "a/b/c.obj" has "a" and "a/b".
std::vector<std::string_view> foldersOf(std::string_view path)
{
    std::vector<std::string_view> folders;
    for (std::size_t slash = path.find('/'); slash != std::string_view::npos;
         slash = path.find('/', slash + 1)) {
        folders.push_back(path.substr(0, slash));
    }
    return folders;
}

struct Probe {
    bool present = false;
    // Why the file is missing, for messages.
    std::string problem;
};

// Looks for the file segment by segment and accepts only exactly the path's
// spelling, so that a wrong letter case is "missing" on Windows as on Linux.
Probe probe(const std::filesystem::path& root, const AssetPath& path)
{
    const std::filesystem::path wanted = path.resolve(root);
    std::filesystem::path current = root;
    std::string onDisk;
    bool caseDiffers = false;
    for (const std::string_view segment : segmentsOf(path.text())) {
        std::optional<std::string> exact;
        std::optional<std::string> otherCase;
        std::error_code error;
        for (std::filesystem::directory_iterator entry(current, error), end; !error && entry != end;
             entry.increment(error)) {
            const std::string name = core::toUtf8(entry->path().filename());
            if (name == segment) {
                exact = name;
                break;
            }
            if (!otherCase && foldCase(name) == foldCase(segment)) {
                otherCase = name;
            }
        }
        const std::optional<std::string>& found = exact ? exact : otherCase;
        if (!found) {
            return {false, std::format("file not found: '{}'", core::toUtf8(wanted))};
        }
        caseDiffers = caseDiffers || !exact;
        current /= std::filesystem::path(*found);
        onDisk += onDisk.empty() ? *found : "/" + *found;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(current, error)) {
        return {false, std::format("not a regular file: '{}'", core::toUtf8(wanted))};
    }
    if (caseDiffers) {
        return {false,
                std::format("file not found with this letter case; on disk it is '{}'", onDisk)};
    }
    return {true, {}};
}

std::string extensionList(AssetType type)
{
    std::string list;
    for (const std::string_view extension : assetExtensions(type)) {
        list += list.empty() ? std::string(extension) : " or " + std::string(extension);
    }
    return list;
}

} // namespace

AssetRegistry::AssetRegistry(std::filesystem::path projectRoot) : root_(std::move(projectRoot)) {}

core::Error AssetRegistry::unknown(AssetId id) const
{
    if (id.isNull()) {
        return core::Error{"asset registry: the null asset ID refers to no asset"};
    }
    return core::Error{std::format("asset {}: not registered", id.toString())};
}

std::optional<core::Error> AssetRegistry::checkPath(AssetId id, const AssetPath& path) const
{
    const std::string asset = std::format("asset {}", id.toString());
    const std::string folded = path.folded();
    if (const auto taken = byFoldedPath_.find(folded); taken != byFoldedPath_.end()) {
        const AssetMetadata& other = byId_.at(taken->second);
        if (other.path == path) {
            return core::Error{std::format("{}: path '{}' is already used by asset {}", asset,
                                           path.text(), taken->second.toString())};
        }
        return core::Error{std::format("{}: path '{}' differs only in letter case from '{}' of "
                                       "asset {}: one file on Windows, two on Linux",
                                       asset, path.text(), other.path.text(),
                                       taken->second.toString())};
    }
    if (folders_.contains(folded)) {
        return core::Error{
            std::format("{}: path '{}' is a folder of registered assets", asset, path.text())};
    }
    for (const std::string_view folder : foldersOf(path.text())) {
        const std::string foldedFolder = foldCase(folder);
        if (const auto file = byFoldedPath_.find(foldedFolder); file != byFoldedPath_.end()) {
            return core::Error{std::format("{}: folder '{}' of '{}' is the file of asset {}", asset,
                                           folder, path.text(), file->second.toString())};
        }
        const auto known = folders_.find(foldedFolder);
        if (known != folders_.end() && known->second.spelling != folder) {
            return core::Error{std::format("{}: folder '{}' of '{}' differs only in letter case "
                                           "from the registered folder '{}'",
                                           asset, folder, path.text(), known->second.spelling)};
        }
    }
    return std::nullopt;
}

std::optional<core::Error> AssetRegistry::registerAsset(AssetId id, AssetType type,
                                                        const AssetPath& path)
{
    if (id.isNull()) {
        return core::Error{"asset registry: the null asset ID is not an identity"};
    }
    if (const auto existing = byId_.find(id); existing != byId_.end()) {
        return core::Error{std::format("asset {}: already registered as {} '{}'", id.toString(),
                                       assetTypeName(existing->second.type),
                                       existing->second.path.text())};
    }
    const auto extensions = assetExtensions(type);
    if (!extensions.empty()) {
        const std::string extension = foldCase(path.extension());
        bool allowed = false;
        for (const std::string_view candidate : extensions) {
            allowed = allowed || extension == candidate;
        }
        if (!allowed) {
            return core::Error{std::format("asset {}: {} '{}' must have the extension {}",
                                           id.toString(), assetTypeName(type), path.text(),
                                           extensionList(type))};
        }
    }
    if (auto error = checkPath(id, path)) {
        return error;
    }

    const Probe found = probe(root_, path);
    byId_.emplace(
        id, AssetMetadata{id, type, path,
                          found.present ? AssetAvailability::Present : AssetAvailability::Missing});
    byFoldedPath_.emplace(path.folded(), id);
    for (const std::string_view folder : foldersOf(path.text())) {
        Folder& entry = folders_[foldCase(folder)];
        entry.spelling = std::string(folder);
        ++entry.uses;
    }
    return std::nullopt;
}

std::optional<core::Error> AssetRegistry::unregisterAsset(AssetId id)
{
    const auto found = byId_.find(id);
    if (found == byId_.end()) {
        return unknown(id);
    }
    const AssetPath path = found->second.path;
    for (const std::string_view folder : foldersOf(path.text())) {
        const auto entry = folders_.find(foldCase(folder));
        if (entry != folders_.end() && --entry->second.uses == 0) {
            folders_.erase(entry);
        }
    }
    byFoldedPath_.erase(path.folded());
    byId_.erase(found);
    return std::nullopt;
}

bool AssetRegistry::contains(AssetId id) const noexcept
{
    return byId_.find(id) != byId_.end();
}

core::Result<AssetMetadata> AssetRegistry::find(AssetId id) const
{
    const auto found = byId_.find(id);
    if (found == byId_.end()) {
        return unknown(id);
    }
    return found->second;
}

std::optional<AssetId> AssetRegistry::findByPath(const AssetPath& path) const
{
    const auto found = byFoldedPath_.find(path.folded());
    if (found == byFoldedPath_.end()) {
        return std::nullopt;
    }
    const auto asset = byId_.find(found->second);
    if (asset == byId_.end() || asset->second.path != path) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<core::Error> AssetRegistry::checkType(AssetId id, AssetType expected) const
{
    const auto found = byId_.find(id);
    if (found == byId_.end()) {
        return unknown(id);
    }
    const AssetMetadata& asset = found->second;
    if (asset.type != expected) {
        return core::Error{std::format("asset {}: '{}' is a {}, expected a {}", id.toString(),
                                       asset.path.text(), assetTypeName(asset.type),
                                       assetTypeName(expected))};
    }
    return std::nullopt;
}

core::Result<std::filesystem::path> AssetRegistry::resolve(AssetId id, AssetType expected) const
{
    if (auto error = checkType(id, expected)) {
        return *error;
    }
    const AssetMetadata& asset = byId_.at(id);
    const Probe found = probe(root_, asset.path);
    if (!found.present) {
        return core::Error{std::format("asset {} ({} '{}'): {}", id.toString(),
                                       assetTypeName(asset.type), asset.path.text(),
                                       found.problem)};
    }
    return asset.path.resolve(root_);
}

std::vector<AssetId> AssetRegistry::refreshAvailability()
{
    std::vector<AssetId> missing;
    for (const auto& entry : byFoldedPath_) {
        AssetMetadata& asset = byId_.at(entry.second);
        asset.availability = probe(root_, asset.path).present ? AssetAvailability::Present
                                                              : AssetAvailability::Missing;
        if (asset.availability == AssetAvailability::Missing) {
            missing.push_back(asset.id);
        }
    }
    return missing;
}

std::vector<AssetMetadata> AssetRegistry::assets() const
{
    std::vector<AssetMetadata> result;
    result.reserve(byFoldedPath_.size());
    for (const auto& entry : byFoldedPath_) {
        result.push_back(byId_.at(entry.second));
    }
    return result;
}

} // namespace engine::assets
