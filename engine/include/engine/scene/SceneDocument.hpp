#pragma once

// A scene as a versioned JSON document (M4.4, ADR 0004, ADR 0023):
//
//   {
//     "format": "lpe.scene",
//     "formatVersion": 1,
//     "id": "<scene ID>",
//     "name": "Main",
//     "entities": [
//       {
//         "id": "<entity ID>",
//         "components": {
//           "name": {"value": "Lighthouse"},
//           "transform": {"position": {"x": 0.0, "y": 12.5, "z": -3.0},
//                         "rotation": {"w": 1.0, "x": 0.0, "y": 0.0, "z": 0.0},
//                         "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
//           "staticMesh": {"mesh": "<asset ID>", "material": null},
//           "camera": {"verticalFovRadians": 1.0471976, "nearPlane": 0.1,
//                      "farPlane": 1000.0},
//           "directionalLight": {"direction": {"x": 0.0, "y": -1.0, "z": 0.0},
//                                "color": {"r": 1.0, "g": 1.0, "b": 1.0},
//                                "intensity": 1.0}
//         }
//       }
//     ]
//   }
//
// Entities are written in creation order, each component an entity has with
// every field, floats with the fewest digits that read back bit for bit. An
// entity without a component has no key for it. Reading is all or nothing
// and builds a new Scene, so a broken document never touches a scene the
// caller already has. Unknown fields are ignored with a warning; an unknown
// component type is an error, so that no component is lost by loading and
// saving again. The JSON library stays inside the implementation; nothing
// here throws.

#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Result.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::scene {

// The "format" of a scene document.
inline constexpr std::string_view kSceneFormat = "lpe.scene";
// The "formatVersion" this build writes and the newest it reads.
inline constexpr int kSceneFormatVersion = 1;
// Limits for documents that may come from outside the project.
inline constexpr std::size_t kMaxSceneEntities = 100000;
inline constexpr std::uintmax_t kMaxSceneDocumentBytes = 64U * 1024U * 1024U;

struct LoadedScene {
    Scene scene;
    // One per field the reader does not know, which is ignored, e.g.
    // "unknown field 'entities[2].components.transform.pivot' ignored".
    std::vector<std::string> warnings;
};

// The document's text: indented by two spaces, ending with a newline. The same
// scene always gives the same bytes.
[[nodiscard]] std::string sceneToJson(const Scene& scene);

// Reads a document's text into a new scene whose generator makes the IDs of
// entities created later. Fails with the field path (or the line and column
// of malformed JSON) for text larger than kMaxSceneDocumentBytes, text that
// is not JSON, another format, a missing or newer format version, a missing
// or wrongly typed field, an invalid or duplicate ID, an unknown component
// type, a component value that fails its validate, more than
// kMaxSceneEntities entities, and, if a registry is given, an asset
// reference it does not know or of another type (without a registry the
// references are not checked; findAssetReferenceProblems can list them).
[[nodiscard]] core::Result<LoadedScene>
sceneFromJson(std::string_view text, core::UuidGenerator generator,
              const assets::AssetRegistry* registry = nullptr);

// Writes sceneToJson() with core::writeFileAtomically: the folder must exist,
// and on failure an existing file stays as it was.
[[nodiscard]] std::optional<core::Error> saveScene(const std::filesystem::path& file,
                                                   const Scene& scene);

// Reads a scene document file (at most kMaxSceneDocumentBytes); errors start
// with the file's name.
[[nodiscard]] core::Result<LoadedScene> loadScene(const std::filesystem::path& file,
                                                  core::UuidGenerator generator,
                                                  const assets::AssetRegistry* registry = nullptr);

} // namespace engine::scene
