#pragma once

// A project (M5.4, ADR 0028): a folder with the project document
// (project.json), the asset manifest, the start scene and the world settings,
// opened and saved as a whole. The project API depends on no editor, UI,
// window or renderer code, so a runtime application can open the same folder.
//
//   <project>/
//     project.json              the project document (ProjectDocument.hpp)
//     assets.json               the asset manifest (ADR 0022)
//     scenes/main.scene.json    the start scene (ADR 0023)
//     worlds/world.json         the world settings (ADR 0004 Amendment 1)
//
// Opening is all or nothing and writes nothing. Saving writes every part and
// stays consistent as a whole: a save that fails or stops at any point leaves
// a project that opens either as the previous save or as the new one, never
// as a mixture (a save journal, ADR 0028).

#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Result.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/project/ProjectDocument.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::project {

// An open project: its folder and every part, as values owned by the caller.
struct Project {
    // The project's folder; the document's paths are relative to it.
    std::filesystem::path root;
    ProjectDocument document;
    // The manifest's assets, registered under `root`.
    assets::AssetRegistry assets;
    scene::Scene startScene;
    worldgen::WorldGenSettings worldSettings;
};

struct OpenedProject {
    Project project;
    // Fields the readers did not know, each with its file's path (e.g.
    // "scenes/main.scene.json: unknown field 'entities[0].color' ignored"),
    // and a note when an interrupted save was found.
    std::vector<std::string> warnings;
};

struct SaveReport {
    // What the save did besides writing the parts, e.g. finishing an earlier
    // interrupted save or removing its leftover files.
    std::vector<std::string> warnings;
};

// A new project in `root` (created if missing), named `name`: an empty
// manifest, an empty scene "Main" with a new ID from `generator`, the default
// world settings, at the default paths, saved as a whole. Fails, writing
// nothing, for an invalid name and for a folder that already holds a project
// (project.json or a save journal) or any of the default files.
[[nodiscard]] core::Result<Project> createProject(const std::filesystem::path& root,
                                                  std::string_view name,
                                                  core::UuidGenerator generator);

// Opens the project in `root`: the document, the manifest into a registry for
// `root`, the start scene checked against it (`generator` makes the IDs of
// entities created later), the world settings. All or nothing: an error
// starts with the part's project-relative path, e.g.
// "scenes/main.scene.json: entities[3].id: ...". After an interrupted save
// the project opens as that save (ADR 0028). Nothing is written.
[[nodiscard]] core::Result<OpenedProject> openProject(const std::filesystem::path& root,
                                                      core::UuidGenerator generator);

// Saves every part of the project into project.root, consistently as a whole
// (ADR 0028). Fails, writing nothing, for a document validate() refuses,
// world settings that do not pass worldgen::validate(), a scene without an ID,
// or a scene that refers to an asset the registry does not know (or of
// another type). A failure while writing leaves the project as the previous
// save (before the save's commit) or as this save (after it); the error says
// which.
[[nodiscard]] core::Result<SaveReport> saveProject(const Project& project);

} // namespace engine::project
