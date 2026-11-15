#pragma once

// A prefab as a versioned JSON document (M4.6, ADR 0004, ADR 0025):
//
//   {
//     "format": "lpe.prefab",
//     "formatVersion": 1,
//     "id": "<the prefab's asset ID>",
//     "name": "House",
//     "entities": [
//       {"localId": 1, "components": {"name": {"value": "Roof"}, ...,
//                                     "parent": {"entity": 2}}},
//       {"localId": 2, "components": {...}}
//     ]
//   }
//
// The components are those of scene documents (ADR 0023) without
// prefabInstance, and a parent names a local ID of the same document instead
// of an entity ID. Reading follows the rules of scene documents: staged and
// all or nothing, unknown fields warned about, unknown component types an
// error, asset references checked against a registry if one is given.
// Writing validates the prefab first and replaces the file atomically.

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Result.hpp"
#include "engine/scene/Prefab.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::scene {

// The "format" of a prefab document.
inline constexpr std::string_view kPrefabFormat = "lpe.prefab";
// The "formatVersion" this build writes and the newest it reads.
inline constexpr int kPrefabFormatVersion = 1;
inline constexpr std::uintmax_t kMaxPrefabDocumentBytes = 16U * 1024U * 1024U;

struct LoadedPrefab {
    // Passes validate().
    Prefab prefab;
    // One per field the reader does not know, which is ignored.
    std::vector<std::string> warnings;
};

// The document's text: indented by two spaces, ending with a newline.
[[nodiscard]] std::string prefabToJson(const Prefab& prefab);

// Reads a document's text. Fails with the field path for everything scene
// documents fail for, for local IDs that are missing, below 1, duplicated or
// not an integer, for a parent that names no local ID of the document, a
// self-parent or a cycle, and, if a registry is given, for an ID that is not
// a registered prefab asset or asset references it does not know.
[[nodiscard]] core::Result<LoadedPrefab>
prefabFromJson(std::string_view text, const assets::AssetRegistry* registry = nullptr);

// Writes prefabToJson() with core::writeFileAtomically; a prefab that fails
// validate() is not written.
[[nodiscard]] std::optional<core::Error> savePrefab(const std::filesystem::path& file,
                                                    const Prefab& prefab);

// Reads a prefab document file (at most kMaxPrefabDocumentBytes); errors start
// with the file's name.
[[nodiscard]] core::Result<LoadedPrefab>
loadPrefab(const std::filesystem::path& file, const assets::AssetRegistry* registry = nullptr);

// The prefab asset `id` of a project: resolved through the registry as a
// Prefab asset, read with the registry's checks, and its document's ID must
// be `id`.
[[nodiscard]] core::Result<LoadedPrefab> loadPrefabAsset(const assets::AssetRegistry& registry,
                                                         assets::AssetId id);

} // namespace engine::scene
