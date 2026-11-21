#pragma once

// Helpers for the project tests (M5.4): the texts of a project's parts,
// the files below a folder, and two different versions of a project's
// content to save.

#include "../assets/TempProject.hpp"

#include "engine/assets/AssetManifest.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/project/Project.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/StaticMeshComponent.hpp"
#include "engine/worldgen/SettingsFile.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace lpe_test {

// What a project holds, as the texts its parts are saved as.
struct ProjectTexts {
    std::string document;
    std::string manifest;
    std::string scene;
    std::string world;

    friend bool operator==(const ProjectTexts&, const ProjectTexts&) = default;
};

inline ProjectTexts textsOf(const engine::project::Project& project)
{
    return {engine::project::projectDocumentToJson(project.document),
            engine::assets::manifestToJson(project.assets),
            engine::scene::sceneToJson(project.startScene),
            engine::worldgen::settingsToJson(project.worldSettings)};
}

inline std::string fileText(const std::filesystem::path& file)
{
    auto text = engine::core::readTextFile(file);
    REQUIRE(text.hasValue());
    return text.value();
}

// The texts of the project's files on disk, at the default paths.
inline ProjectTexts textsOnDisk(const std::filesystem::path& root)
{
    return {fileText(root / "project.json"), fileText(root / "assets.json"),
            fileText(root / "scenes" / "main.scene.json"),
            fileText(root / "worlds" / "world.json")};
}

// Every file below a folder as a '/'-separated relative path, sorted.
inline std::vector<std::string> filesBelow(const std::filesystem::path& root)
{
    std::vector<std::string> files;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator entry(root, error), end;
         !error && entry != end; entry.increment(error)) {
        if (entry->is_regular_file()) {
            files.push_back(std::filesystem::relative(entry->path(), root).generic_string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

inline const std::vector<std::string> kDefaultFiles{
    "assets.json",
    "project.json",
    "scenes/main.scene.json",
    "worlds/world.json",
};

// Version `version` (1, 2, 3, ...) of a project's content in `root`: its own
// name, `version` mesh assets (their files written), as many entities using
// them, and its own world seed. Every part differs between versions.
inline engine::project::Project projectVersion(const TempProject& folder,
                                               const std::filesystem::path& root, int version)
{
    auto document = engine::project::defaultProjectDocument(std::format("Version {}", version));
    REQUIRE(document.hasValue());
    engine::assets::AssetRegistry registry(root);
    auto scene = engine::scene::Scene::create(
        engine::scene::SceneId{engine::core::Uuid{0x5CE7E00000004000ULL, 0x8000000000000001ULL}},
        "Main", engine::core::UuidGenerator{static_cast<std::uint64_t>(version)});
    REQUIRE(scene.hasValue());
    for (int i = 1; i <= version; ++i) {
        const std::string path = std::format("models/v{}-{}.obj", version, i);
        const std::filesystem::path relativeToFolder =
            std::filesystem::relative(root / path, folder.root());
        folder.write(relativeToFolder.generic_string(), "v 0 0 0\n");
        auto assetPath = engine::assets::AssetPath::parse(path);
        REQUIRE(assetPath.hasValue());
        const engine::assets::AssetId mesh = assetId(static_cast<std::uint64_t>(100 * version + i));
        REQUIRE_FALSE(
            registry.registerAsset(mesh, engine::assets::AssetType::Mesh, assetPath.value())
                .has_value());
        auto entity = scene.value().createEntity();
        REQUIRE(entity.hasValue());
        REQUIRE_FALSE(scene.value()
                          .addComponent(entity.value(),
                                        engine::scene::NameComponent{std::format("Tower {}", i)})
                          .has_value());
        REQUIRE_FALSE(
            scene.value()
                .addComponent(entity.value(), engine::scene::StaticMeshComponent{mesh, {}})
                .has_value());
    }
    engine::worldgen::WorldGenSettings settings;
    settings.seed = static_cast<std::uint64_t>(1000 + version);
    return engine::project::Project{root, std::move(document).value(), std::move(registry),
                                    std::move(scene).value(), settings};
}

} // namespace lpe_test
