// Prefabs in a project (M4.6): a prefab saved into a temporary project and
// registered in its manifest is loaded through its asset ID and instantiated
// twice into a loaded scene, which is saved and loaded again.

#include "engine/assets/AssetManifest.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Prefab.hpp"
#include "engine/scene/PrefabDocument.hpp"
#include "engine/scene/SceneDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::core::UuidGenerator;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::PrefabInstanceComponent;
using engine::scene::Scene;
using engine::scene::TransformComponent;

namespace {

class TempProject {
public:
    TempProject() : root_(std::filesystem::temp_directory_path() / "lpe_integration_prefabs")
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::create_directories(root_ / "prefabs");
        std::filesystem::create_directories(root_ / "scenes");
    }
    ~TempProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

} // namespace

TEST_CASE("A registered prefab is instantiated twice into a loaded scene", "[integration][prefab]")
{
    const TempProject project;
    UuidGenerator generator(1);
    const AssetId lampId{generator.next()};

    // A lamp prefab made from a small scene: a post with a light on top.
    {
        Scene workshop = Scene::create("Workshop", generator).value();
        const EntityId post = workshop.createEntity().value();
        REQUIRE_FALSE(workshop.addComponent(post, NameComponent{"Post"}).has_value());
        REQUIRE_FALSE(workshop.addComponent(post, TransformComponent{}).has_value());
        const EntityId light = workshop.createEntity().value();
        REQUIRE_FALSE(workshop.addComponent(light, NameComponent{"Light"}).has_value());
        TransformComponent top;
        top.position = {0.0F, 2.5F, 0.0F};
        REQUIRE_FALSE(workshop.addComponent(light, top).has_value());
        REQUIRE_FALSE(workshop.addComponent(light, ParentComponent{post}).has_value());
        const std::vector<EntityId> parts{post, light};
        auto prefab = engine::scene::createPrefab(workshop, parts, lampId, "Lamp");
        REQUIRE(prefab);
        REQUIRE_FALSE(
            engine::scene::savePrefab(project.root() / "prefabs" / "lamp.prefab", prefab.value())
                .has_value());

        AssetRegistry registry(project.root());
        REQUIRE_FALSE(registry
                          .registerAsset(lampId, AssetType::Prefab,
                                         AssetPath::parse("prefabs/lamp.prefab").value())
                          .has_value());
        REQUIRE_FALSE(
            engine::assets::saveManifest(project.root() / "assets.json", registry).has_value());

        Scene street = Scene::create("Street", generator).value();
        (void)street.createEntity().value();
        REQUIRE_FALSE(
            engine::scene::saveScene(project.root() / "scenes" / "street.scene.json", street)
                .has_value());
    }

    const auto manifest =
        engine::assets::loadManifest(project.root() / "assets.json", project.root());
    REQUIRE(manifest);
    const AssetRegistry& registry = manifest.value().registry;
    const std::filesystem::path sceneFile = project.root() / "scenes" / "street.scene.json";
    auto loadedScene = engine::scene::loadScene(sceneFile, UuidGenerator(2), &registry);
    REQUIRE(loadedScene);
    Scene street = std::move(loadedScene).value().scene;

    const auto lamp = engine::scene::loadPrefabAsset(registry, lampId);
    REQUIRE(lamp);
    const auto first = engine::scene::instantiatePrefab(street, lamp.value().prefab);
    const auto second = engine::scene::instantiatePrefab(street, lamp.value().prefab);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE_FALSE(street
                      .replaceComponent(first.value().entities[0],
                                        [] {
                                            TransformComponent moved;
                                            moved.position = {10.0F, 0.0F, 0.0F};
                                            return moved;
                                        }())
                      .has_value());
    REQUIRE_FALSE(engine::scene::saveScene(sceneFile, street).has_value());

    auto reloaded = engine::scene::loadScene(sceneFile, UuidGenerator(3), &registry);
    REQUIRE(reloaded);
    const Scene& result = reloaded.value().scene;
    CHECK(result.entityCount() == 5);
    CHECK(engine::scene::sceneToJson(result) == engine::scene::sceneToJson(street));
    std::set<EntityId> ids;
    for (const auto* instance : {&first.value(), &second.value()}) {
        const EntityId post = instance->entities[0];
        const EntityId light = instance->entities[1];
        ids.insert(post);
        ids.insert(light);
        CHECK(result.component<ParentComponent>(light).value().entity == post);
        CHECK(result.component<PrefabInstanceComponent>(post).value().prefab == lampId);
        CHECK(result.component<PrefabInstanceComponent>(light).value().instance == instance->id);
        CHECK(result.component<NameComponent>(light).value() == NameComponent{"Light"});
    }
    CHECK(ids.size() == 4);
    CHECK(result.component<TransformComponent>(first.value().entities[0]).value().position.x ==
          10.0F);
    CHECK(result.component<TransformComponent>(second.value().entities[0]).value().position.x ==
          0.0F);
}
