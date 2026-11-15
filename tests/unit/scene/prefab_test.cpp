#include "../assets/TempProject.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Prefab.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using engine::core::Uuid;
using engine::core::UuidGenerator;
using engine::math::Vec3;
using engine::scene::EntityComponents;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::Prefab;
using engine::scene::PrefabEntity;
using engine::scene::PrefabInstanceComponent;
using engine::scene::Scene;
using engine::scene::SceneId;
using engine::scene::ScriptComponent;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;
using lpe_test::assetId;

namespace {

Scene makeScene(std::uint64_t seed = 31)
{
    auto created = Scene::create(SceneId{Uuid{0x0123456789ABCDEFULL, 0x8000000000000000ULL | seed}},
                                 "Main", UuidGenerator(seed));
    REQUIRE(created);
    return std::move(created).value();
}

TransformComponent at(float x, float y, float z)
{
    TransformComponent transform;
    transform.position = {x, y, z};
    return transform;
}

// A house: a base with a roof and a door, the door with a script.
struct House {
    EntityId base;
    EntityId roof;
    EntityId door;
};

House buildHouse(Scene& scene)
{
    House house{scene.createEntity().value(), scene.createEntity().value(),
                scene.createEntity().value()};
    REQUIRE_FALSE(scene.addComponent(house.base, NameComponent{"Base"}).has_value());
    REQUIRE_FALSE(scene.addComponent(house.base, at(0, 0, 0)).has_value());
    REQUIRE_FALSE(
        scene.addComponent(house.base, StaticMeshComponent{assetId(1), assetId(2)}).has_value());
    REQUIRE_FALSE(scene.addComponent(house.roof, NameComponent{"Roof"}).has_value());
    REQUIRE_FALSE(scene.addComponent(house.roof, at(0, 3, 0)).has_value());
    REQUIRE_FALSE(scene.addComponent(house.roof, ParentComponent{house.base}).has_value());
    REQUIRE_FALSE(scene.addComponent(house.door, NameComponent{"Door"}).has_value());
    REQUIRE_FALSE(scene.addComponent(house.door, at(1, 0, 2)).has_value());
    REQUIRE_FALSE(scene.addComponent(house.door, ScriptComponent{assetId(3), true}).has_value());
    REQUIRE_FALSE(scene.addComponent(house.door, ParentComponent{house.base}).has_value());
    return house;
}

Prefab housePrefab()
{
    Scene scene = makeScene();
    const House house = buildHouse(scene);
    const std::vector<EntityId> entities{house.base, house.roof, house.door};
    auto prefab = engine::scene::createPrefab(scene, entities, assetId(10), "House");
    INFO((prefab ? std::string{} : prefab.error().message));
    REQUIRE(prefab);
    return std::move(prefab).value();
}

std::string errorOf(const Prefab& prefab)
{
    const auto error = engine::scene::validate(prefab);
    REQUIRE(error.has_value());
    return error->message;
}

} // namespace

TEST_CASE("A prefab is made from scene entities with their links", "[scene][prefab]")
{
    const Prefab prefab = housePrefab();
    CHECK(prefab.id == assetId(10));
    CHECK(prefab.name == "House");
    REQUIRE(prefab.entities.size() == 3);
    CHECK(prefab.entities[0].localId == 1);
    CHECK(prefab.entities[1].localId == 2);
    CHECK(prefab.entities[2].localId == 3);
    CHECK_FALSE(prefab.entities[0].parent.has_value());
    CHECK(prefab.entities[1].parent == 1U);
    CHECK(prefab.entities[2].parent == 1U);
    CHECK(prefab.entities[0].components.staticMesh == StaticMeshComponent{assetId(1), assetId(2)});
    CHECK(prefab.entities[1].components.name == NameComponent{"Roof"});
    CHECK(prefab.entities[2].components.script == ScriptComponent{assetId(3), true});
    CHECK(prefab.entities[2].components.transform->position == Vec3{1.0F, 0.0F, 2.0F});
    CHECK_FALSE(engine::scene::validate(prefab).has_value());
}

TEST_CASE("A prefab is made only from a closed set of scene entities", "[scene][prefab]")
{
    Scene scene = makeScene();
    const House house = buildHouse(scene);
    const auto failure = [&](std::vector<EntityId> entities) {
        auto prefab = engine::scene::createPrefab(scene, entities, assetId(10), "House");
        REQUIRE_FALSE(prefab);
        return prefab.error().message;
    };
    CHECK(failure({house.roof}) == "prefab 'House': entity " + house.roof.toString() +
                                       " has the parent " + house.base.toString() +
                                       ", which is not one of the prefab's entities");
    CHECK(failure({}) == "prefab 'House': no entities given");
    CHECK(failure({house.base, house.base}) ==
          "prefab 'House': entity " + house.base.toString() + " is listed twice");
    const EntityId unknown{Uuid{0x1234ULL, 0x8000000000001234ULL}};
    CHECK(failure({unknown}) ==
          "prefab 'House': scene 'Main': entity " + unknown.toString() + ": does not exist");
    const std::vector<EntityId> base{house.base};
    CHECK(engine::scene::createPrefab(scene, base, {}, "House").error().message ==
          "prefab 'House': id: the null asset ID is not an identity");
    CHECK(engine::scene::createPrefab(scene, base, assetId(10), "").error().message ==
          "prefab: name: must not be empty");

    // A prefab of instance entities is a plain copy: no nested prefabs.
    auto instance = engine::scene::instantiatePrefab(scene, housePrefab());
    REQUIRE(instance);
    const std::vector<EntityId> copied{instance.value().entities[0]};
    const auto plain = engine::scene::createPrefab(scene, copied, assetId(11), "Base");
    REQUIRE(plain);
    CHECK(plain.value().entities[0].components == housePrefab().entities[0].components);
}

TEST_CASE("A prefab's local IDs, links and components are checked", "[scene][prefab]")
{
    Prefab prefab = housePrefab();
    prefab.entities[2].localId = 1;
    CHECK(errorOf(prefab) == "prefab 'House': entities[2].localId: 1 is also entities[0]");
    prefab.entities[2].localId = 0;
    CHECK(errorOf(prefab) == "prefab 'House': entities[2].localId: 0, must be at least 1");

    prefab = housePrefab();
    prefab.entities[1].parent = 9;
    CHECK(errorOf(prefab) == "prefab 'House': entities[1].components.parent.entity: 9 is not "
                             "the local ID of an entity of the prefab");
    prefab.entities[1].parent = 2;
    CHECK(errorOf(prefab) == "prefab 'House': entities[1].components.parent.entity: an entity "
                             "cannot be its own parent");
    prefab.entities[1].parent = 3;
    prefab.entities[2].parent = 2;
    CHECK(errorOf(prefab) == "prefab 'House': entities[1].components.parent.entity: entity 3 is "
                             "a descendant of this entity, which makes a cycle");

    prefab = housePrefab();
    prefab.entities[0].components.transform->scale = Vec3{0.0F};
    CHECK(errorOf(prefab) == "prefab 'House': entities[0]: transform component: scale.x is 0, "
                             "must be in [0.0001, 10000]");

    prefab = housePrefab();
    prefab.entities.clear();
    CHECK(errorOf(prefab) == "prefab 'House': entities: a prefab needs at least one entity");
    prefab.entities.resize(engine::scene::kMaxPrefabEntities + 1);
    CHECK(errorOf(prefab) == "prefab 'House': entities: 10001 entries, at most 10000 allowed");
}

TEST_CASE("Two instances of a prefab are independent", "[scene][prefab]")
{
    const Prefab prefab = housePrefab();
    Scene scene = makeScene(40);
    const auto first = engine::scene::instantiatePrefab(scene, prefab);
    const auto second = engine::scene::instantiatePrefab(scene, prefab);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(scene.entityCount() == 6);
    CHECK(first.value().id != second.value().id);

    std::set<EntityId> ids;
    for (const auto* instance : {&first.value(), &second.value()}) {
        REQUIRE(instance->entities.size() == 3);
        const EntityId base = instance->entities[0];
        for (std::size_t i = 0; i < 3; ++i) {
            const EntityId entity = instance->entities[i];
            ids.insert(entity);
            CHECK(engine::scene::componentsOf(scene, entity) == prefab.entities[i].components);
            const auto link = scene.component<PrefabInstanceComponent>(entity).value();
            CHECK(link ==
                  PrefabInstanceComponent{assetId(10), instance->id, prefab.entities[i].localId});
        }
        // Links point inside their own instance.
        CHECK_FALSE(scene.hasComponent<ParentComponent>(base));
        CHECK(scene.component<ParentComponent>(instance->entities[1]).value().entity == base);
        CHECK(scene.component<ParentComponent>(instance->entities[2]).value().entity == base);
    }
    CHECK(ids.size() == 6);

    // Changing one instance leaves the other and the prefab as they were.
    REQUIRE_FALSE(scene.replaceComponent(first.value().entities[1], at(5, 5, 5)).has_value());
    REQUIRE_FALSE(scene.destroyEntity(first.value().entities[0]).has_value());
    CHECK(scene.component<TransformComponent>(second.value().entities[1]).value().position ==
          Vec3{0.0F, 3.0F, 0.0F});
    CHECK(scene.component<ParentComponent>(second.value().entities[1]).value().entity ==
          second.value().entities[0]);
    CHECK_FALSE(scene.hasComponent<ParentComponent>(first.value().entities[1]));
    CHECK(prefab == housePrefab());
}

TEST_CASE("A failed instantiation leaves the scene unchanged", "[scene][prefab]")
{
    Scene scene = makeScene(41);
    REQUIRE(engine::scene::instantiatePrefab(scene, housePrefab()));
    const std::string before = engine::scene::sceneToJson(scene);

    Prefab broken = housePrefab();
    broken.entities[2].parent = 3;
    const auto failed = engine::scene::instantiatePrefab(scene, broken);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message.ends_with("an entity cannot be its own parent"));
    CHECK(engine::scene::sceneToJson(scene) == before);
}

TEST_CASE("A scene with prefab instances reads back with links and instance data",
          "[scene][prefab][document]")
{
    Scene scene = makeScene(42);
    const auto instance = engine::scene::instantiatePrefab(scene, housePrefab());
    REQUIRE(instance);
    const std::string text = engine::scene::sceneToJson(scene);
    CHECK(text.find("\"prefabInstance\": {\n          \"prefab\": \"" + assetId(10).toString() +
                    "\",\n          \"instance\": \"" + instance.value().id.toString() +
                    "\",\n          \"localId\": 3\n") != std::string::npos);
    auto loaded = engine::scene::sceneFromJson(text, UuidGenerator(1));
    REQUIRE(loaded);
    const Scene& copy = loaded.value().scene;
    for (const EntityId entity : instance.value().entities) {
        CHECK(copy.component<PrefabInstanceComponent>(entity).value() ==
              scene.component<PrefabInstanceComponent>(entity).value());
        CHECK(copy.hasComponent<ParentComponent>(entity) ==
              scene.hasComponent<ParentComponent>(entity));
    }
    CHECK(engine::scene::sceneToJson(copy) == text);

    // The instance link is checked like any other component.
    const auto errorOf = [&](const std::string& from, const std::string& to) {
        std::string changed = text;
        changed.replace(changed.find(from), from.size(), to);
        const auto result = engine::scene::sceneFromJson(changed, UuidGenerator(1));
        REQUIRE_FALSE(result);
        return result.error().message;
    };
    CHECK(errorOf("\"localId\": 3", "\"localId\": 0")
              .ends_with(".components.prefabInstance: localId is 0, must be at least 1"));
    CHECK(errorOf("\"localId\": 3", "\"localId\": -3")
              .ends_with(".components.prefabInstance.localId: -3 is outside the range of the "
                         "field's type (0 to 4294967295)"));
    CHECK(errorOf("\"instance\": \"" + instance.value().id.toString() + "\"", "\"instance\": \"x\"")
              .ends_with(".components.prefabInstance.instance: prefab instance ID 'x': "
                         "expected 36 characters in the form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, "
                         "found 1"));

    // With a registry, the prefab must be a registered prefab asset.
    engine::assets::AssetRegistry registry(std::filesystem::path("unused"));
    for (const auto& [number, type, path] :
         {std::tuple{1U, engine::assets::AssetType::Mesh, "a.obj"},
          std::tuple{2U, engine::assets::AssetType::Material, "a.mat"},
          std::tuple{3U, engine::assets::AssetType::Script, "a.lua"}}) {
        REQUIRE_FALSE(registry
                          .registerAsset(assetId(number), type,
                                         engine::assets::AssetPath::parse(path).value())
                          .has_value());
    }
    const auto unregistered = engine::scene::sceneFromJson(text, UuidGenerator(1), &registry);
    REQUIRE_FALSE(unregistered);
    CHECK(unregistered.error().message.ends_with(".components.prefabInstance.prefab: asset " +
                                                 assetId(10).toString() + ": not registered"));
    REQUIRE_FALSE(registry
                      .registerAsset(assetId(10), engine::assets::AssetType::Prefab,
                                     engine::assets::AssetPath::parse("house.prefab").value())
                      .has_value());
    CHECK(engine::scene::sceneFromJson(text, UuidGenerator(1), &registry));
}
