#pragma once

// Helpers for the editor document tests (M5.5): a project in memory with an
// asset of each type, the document's texts, and short ways to make commands.

#include "../assets/TempProject.hpp"

#include "editor/commands/ComponentCommands.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "editor/commands/WorldCommands.hpp"
#include "editor/document/EditorDocument.hpp"

#include "engine/project/Project.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/worldgen/SettingsFile.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lpe_test {

// Assets of the in-memory project.
inline const engine::assets::AssetId kMesh = assetId(1);
inline const engine::assets::AssetId kOtherMesh = assetId(2);
inline const engine::assets::AssetId kMaterial = assetId(3);
inline const engine::assets::AssetId kScript = assetId(4);
inline const engine::assets::AssetId kPrefab = assetId(5);
// Registered nowhere.
inline const engine::assets::AssetId kUnknownAsset = assetId(99);

inline engine::assets::AssetPath assetPath(std::string_view text)
{
    auto path = engine::assets::AssetPath::parse(text);
    REQUIRE(path.hasValue());
    return path.value();
}

// A project that exists only in memory (its folder is never written unless a
// test saves it): an empty scene "Main" and one asset of each type.
inline engine::project::Project
memoryProject(const std::filesystem::path& root = std::filesystem::path("unsaved-project"),
              std::uint64_t seed = 1)
{
    auto document = engine::project::defaultProjectDocument("Test");
    REQUIRE(document.hasValue());
    engine::assets::AssetRegistry assets(root);
    using engine::assets::AssetType;
    REQUIRE_FALSE(
        assets.registerAsset(kMesh, AssetType::Mesh, assetPath("models/a.obj")).has_value());
    REQUIRE_FALSE(
        assets.registerAsset(kOtherMesh, AssetType::Mesh, assetPath("models/b.obj")).has_value());
    REQUIRE_FALSE(
        assets.registerAsset(kMaterial, AssetType::Material, assetPath("materials/a.material"))
            .has_value());
    REQUIRE_FALSE(
        assets.registerAsset(kScript, AssetType::Script, assetPath("scripts/a.lua")).has_value());
    REQUIRE_FALSE(
        assets.registerAsset(kPrefab, AssetType::Prefab, assetPath("prefabs/a.prefab.json"))
            .has_value());
    auto scene = engine::scene::Scene::create(
        engine::scene::SceneId{engine::core::Uuid{0x5CE7E00000004000ULL, 0x8000000000000002ULL}},
        "Main", engine::core::UuidGenerator{seed});
    REQUIRE(scene.hasValue());
    return engine::project::Project{root, std::move(document).value(), std::move(assets),
                                    std::move(scene).value(), engine::worldgen::WorldGenSettings{}};
}

// The document's content as the texts it is saved as.
struct DocumentTexts {
    std::string scene;
    std::string world;

    friend bool operator==(const DocumentTexts&, const DocumentTexts&) = default;
};

inline DocumentTexts textsOf(const editor::EditorDocument& document)
{
    return {engine::scene::sceneToJson(document.scene()),
            engine::worldgen::settingsToJson(document.worldSettings())};
}

// Executes a command that must succeed; returns it for its results (e.g. the
// created entity), valid while the document keeps it.
template <typename C>
const C& run(editor::EditorDocument& document, std::unique_ptr<C> command,
             editor::MergeKey key = editor::kNoMerge)
{
    const C* raw = command.get();
    const auto error = document.execute(std::move(command), key);
    INFO((error ? error->message : std::string()));
    REQUIRE_FALSE(error.has_value());
    return *raw;
}

inline engine::scene::EntityId create(editor::EditorDocument& document,
                                      std::vector<editor::ComponentValue> components = {})
{
    return run(document, std::make_unique<editor::CreateEntity>(std::move(components))).entity();
}

inline engine::scene::NameComponent name(std::string value)
{
    return engine::scene::NameComponent{std::move(value)};
}

inline engine::scene::TransformComponent at(float x, float y = 0.0F, float z = 0.0F)
{
    engine::scene::TransformComponent transform;
    transform.position = engine::math::Vec3{x, y, z};
    return transform;
}

} // namespace lpe_test
