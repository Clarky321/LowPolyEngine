#pragma once

// The project document (M5.4, ADR 0028): one versioned JSON document that
// names the parts of a project, by project-relative paths:
//
//   {
//     "format": "lpe.project",
//     "formatVersion": 1,
//     "name": "Sky Islands",
//     "assetManifest": "assets.json",
//     "startScene": "scenes/main.scene.json",
//     "worldSettings": "worlds/world.json"
//   }
//
// The document has the fixed name project.json in the project's folder. It
// is a value; reading and writing the files of a project is Project.hpp's.
// The JSON library stays inside the implementation; nothing here throws.

#include "engine/assets/AssetPath.hpp"
#include "engine/core/Result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::project {

// The "format" of a project document.
inline constexpr std::string_view kProjectFormat = "lpe.project";
// The "formatVersion" this build writes and the newest it reads.
inline constexpr int kProjectFormatVersion = 1;
// The project document's file name in the project's folder.
inline constexpr std::string_view kProjectFileName = "project.json";
// The save journal's file name (ADR 0028), reserved like the document's.
inline constexpr std::string_view kSaveJournalFileName = "project.saving.json";
// Limit for a document that may come from outside the project.
inline constexpr std::uintmax_t kMaxProjectDocumentBytes = 1024U * 1024U;

// The paths of a new project's parts (ADR 0028), and its scene's name.
inline constexpr std::string_view kDefaultManifestPath = "assets.json";
inline constexpr std::string_view kDefaultStartScenePath = "scenes/main.scene.json";
inline constexpr std::string_view kDefaultWorldSettingsPath = "worlds/world.json";
inline constexpr std::string_view kDefaultSceneName = "Main";

struct ProjectDocument {
    // Under the rules of scene names (scene::validateName).
    std::string name;
    assets::AssetPath assetManifest;
    assets::AssetPath startScene;
    assets::AssetPath worldSettings;
};

// A document with `name` and the default paths. Fails for an invalid name.
[[nodiscard]] core::Result<ProjectDocument> defaultProjectDocument(std::string_view name);

// Nothing if the document can be saved and opened, otherwise the first
// problem: an invalid name; two paths that are the same, also when letter
// case is ignored; a path that is project.json or project.saving.json; a path
// inside another one ("a" and "a/b"). The error names the field, e.g.
// "startScene: 'assets.json' is also the assetManifest".
[[nodiscard]] std::optional<core::Error> validate(const ProjectDocument& document);

struct LoadedProjectDocument {
    ProjectDocument document;
    // One per field the reader does not know, which is ignored, e.g.
    // "unknown field 'author' ignored".
    std::vector<std::string> warnings;
};

// The document's text: the fields in the order above, indented by two spaces,
// ending with a newline. The same document always gives the same bytes.
[[nodiscard]] std::string projectDocumentToJson(const ProjectDocument& document);

// Reads a document's text. Fails with the field (or the line and column of
// malformed JSON) for text that is not JSON or not an object, another format,
// a missing or newer format version, a missing or wrongly typed field, an
// invalid name or path, and every problem validate() finds.
[[nodiscard]] core::Result<LoadedProjectDocument> projectDocumentFromJson(std::string_view text);

} // namespace engine::project
