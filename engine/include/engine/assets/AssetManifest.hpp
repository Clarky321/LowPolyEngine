#pragma once

// The asset manifest: one versioned JSON document that lists every asset of a
// project with its ID, type and path, so asset IDs survive restarts and a
// moved project (M4.3, ADR 0004, ADR 0022):
//
//   {
//     "format": "lpe.assets.manifest",
//     "formatVersion": 1,
//     "assets": [
//       {"id": "6f1c2b9e-4d3a-4f58-9a7e-2c5d8b0e1f43", "type": "mesh",
//        "path": "models/lighthouse.obj"}
//     ]
//   }
//
// Assets are written in the registry's path order, so the file diffs cleanly.
// The project root is not in the file. The JSON library stays inside the
// implementation; nothing here throws.

#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets {

// The "format" of a manifest.
inline constexpr std::string_view kManifestFormat = "lpe.assets.manifest";
// The "formatVersion" this build writes and the newest it reads.
inline constexpr int kManifestFormatVersion = 1;
// Limits for a manifest that may come from outside the project.
inline constexpr std::size_t kMaxManifestAssets = 100000;
inline constexpr std::uintmax_t kMaxManifestBytes = 64U * 1024U * 1024U;

struct LoadedManifest {
    // Every asset of the file, registered under the given project root, with
    // the availability of its file checked.
    AssetRegistry registry;
    // One per field the reader does not know, which is ignored, e.g.
    // "unknown field 'assets[2].name' ignored".
    std::vector<std::string> warnings;
};

// The manifest's text: indented by two spaces, ending with a newline.
[[nodiscard]] std::string manifestToJson(const AssetRegistry& registry);

// Reads a manifest's text into a new registry under the project root. All or
// nothing: fails with a message that names the field (or the line and column
// of malformed JSON) for text that is not JSON, another format, a newer
// format version, a missing or wrongly typed field, an invalid ID, type or
// path, more than kMaxManifestAssets assets, and every conflict the registry
// rejects (duplicate IDs or paths, paths that differ only in letter case).
// Missing files are not errors: those assets are Missing.
[[nodiscard]] core::Result<LoadedManifest>
manifestFromJson(std::string_view text, const std::filesystem::path& projectRoot);

// Writes manifestToJson() to a file, creating missing folders.
[[nodiscard]] std::optional<core::Error> saveManifest(const std::filesystem::path& file,
                                                      const AssetRegistry& registry);

// Reads a manifest file (at most kMaxManifestBytes); errors start with the
// file's name.
[[nodiscard]] core::Result<LoadedManifest> loadManifest(const std::filesystem::path& file,
                                                        const std::filesystem::path& projectRoot);

} // namespace engine::assets
