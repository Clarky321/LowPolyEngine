// The committed test project tests/projects/lighthouses (M5.6; two parent
// links since M5.7): the project lpe-editor opens in the GPU tier, in CI and
// for docs/screenshots/m5. It opens without a warning, holds what the tests
// expect, its lighthouse and world settings are the engine's committed ones,
// and it is saved exactly as the engine saves it (a save of a copy writes the
// same bytes). Headless.

#include "ProjectTestSupport.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/project/Project.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ParentComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/world/SceneDraws.hpp"
#include "engine/worldgen/SettingsFile.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

const std::filesystem::path kLighthouses =
    std::filesystem::path(LPE_TEST_PROJECTS_DIRECTORY) / "lighthouses";

std::string bytesOf(const std::filesystem::path& file)
{
    auto text = engine::core::readTextFile(file);
    REQUIRE(text.hasValue());
    return text.value();
}

} // namespace

TEST_CASE("The lighthouses test project opens without a warning and holds its scene",
          "[project][fixture]")
{
    auto opened = engine::project::openProject(kLighthouses, engine::core::UuidGenerator{1});
    INFO((opened ? std::string() : opened.error().message));
    REQUIRE(opened.hasValue());
    CHECK(opened.value().warnings.empty());
    const engine::project::Project& project = opened.value().project;
    CHECK(project.document.name == "Lighthouses");

    // One mesh, present: the engine's lighthouse, byte for byte.
    const auto assets = project.assets.assets();
    REQUIRE(assets.size() == 1);
    CHECK(assets[0].type == engine::assets::AssetType::Mesh);
    CHECK(assets[0].path.text() == "models/lighthouse.obj");
    CHECK(assets[0].availability == engine::assets::AssetAvailability::Present);
    CHECK(bytesOf(kLighthouses / "models" / "lighthouse.obj") ==
          bytesOf(std::filesystem::path(LPE_ASSET_DIRECTORY) / "models" / "lighthouse.obj"));

    // The default world, as assets/worlds/default.json holds it.
    CHECK(engine::worldgen::settingsToJson(project.worldSettings) ==
          engine::worldgen::settingsToJson(engine::worldgen::WorldGenSettings{}));
    CHECK(bytesOf(kLighthouses / "worlds" / "world.json") ==
          bytesOf(std::filesystem::path(LPE_ASSET_DIRECTORY) / "worlds" / "default.json"));

    // Seven entities: a light, five lighthouses (the mesh entities the
    // viewport draws) and a camera, which is not drawn.
    const engine::scene::Scene& scene = project.startScene;
    CHECK(scene.name() == "Main");
    CHECK(scene.entityCount() == 7);
    CHECK(scene.entitiesWith<engine::scene::DirectionalLightComponent>().size() == 1);
    CHECK(scene.entitiesWith<engine::scene::CameraComponent>().size() == 1);
    const auto draws = engine::world::meshDraws(scene);
    REQUIRE(draws.size() == 5);
    for (const auto& draw : draws) {
        CHECK(draw.mesh == assets[0].id);
        CHECK(scene.hasComponent<engine::scene::TransformComponent>(draw.entity));
    }
    // Two parent links (M5.7), for the hierarchy: the floating lighthouse
    // under the one on the high island, the camera under the one on the near
    // island. They move nothing: transforms are not inherited.
    const auto named = [&scene](std::string_view name) {
        for (const auto entity : scene.entityIds()) {
            const auto component = scene.component<engine::scene::NameComponent>(entity);
            if (component && component.value().value == name) {
                return entity;
            }
        }
        return engine::scene::EntityId{};
    };
    const auto parentOf = [&scene](engine::scene::EntityId entity) {
        const auto link = scene.component<engine::scene::ParentComponent>(entity);
        return link ? link.value().entity : engine::scene::EntityId{};
    };
    CHECK(scene.entitiesWith<engine::scene::ParentComponent>().size() == 2);
    CHECK(parentOf(named("Floating lighthouse")) == named("Lighthouse on the high island"));
    CHECK(parentOf(named("Harbour camera")) == named("Lighthouse on the near island"));
    CHECK_FALSE(named("Lighthouse on the high island").isNull());
}

TEST_CASE("The lighthouses test project is saved exactly as the engine saves it",
          "[project][fixture]")
{
    const lpe_test::TempProject copy("lpe_unit_test_project_copy");
    std::filesystem::copy(kLighthouses, copy.root(),
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing);
    const std::vector<std::string> files = lpe_test::filesBelow(copy.root());
    CHECK(files == std::vector<std::string>{"assets.json", "models/lighthouse.obj", "project.json",
                                            "scenes/main.scene.json", "worlds/world.json"});

    auto opened = engine::project::openProject(copy.root(), engine::core::UuidGenerator{1});
    REQUIRE(opened.hasValue());
    const auto saved = engine::project::saveProject(opened.value().project);
    REQUIRE(saved.hasValue());
    CHECK(saved.value().warnings.empty());
    CHECK(lpe_test::filesBelow(copy.root()) == files);
    CHECK(lpe_test::textsOnDisk(copy.root()) == lpe_test::textsOnDisk(kLighthouses));
}
