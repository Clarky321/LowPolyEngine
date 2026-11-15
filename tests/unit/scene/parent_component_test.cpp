#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using engine::core::Uuid;
using engine::core::UuidGenerator;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::Scene;
using engine::scene::SceneId;
using engine::scene::TransformComponent;

namespace {

Scene makeScene(std::string_view name = "Main", std::uint64_t seed = 21)
{
    auto created = Scene::create(SceneId{Uuid{0x0123456789ABCDEFULL, 0x8000000000000000ULL | seed}},
                                 name, UuidGenerator(seed));
    REQUIRE(created);
    return std::move(created).value();
}

EntityId makeEntity(Scene& scene)
{
    return scene.createEntity().value();
}

std::string messageOf(const std::optional<engine::core::Error>& error)
{
    REQUIRE(error.has_value());
    return error->message;
}

std::string prefix(const Scene& scene, EntityId entity)
{
    return "scene '" + scene.name() + "': entity " + entity.toString() + ": ";
}

} // namespace

TEST_CASE("A parent link needs a parent", "[scene][parent]")
{
    CHECK(engine::scene::validate(ParentComponent{})->message ==
          "parent component: entity is the null entity ID, must name the parent");
    Scene scene = makeScene();
    const EntityId child = makeEntity(scene);
    CHECK(messageOf(scene.addComponent(child, ParentComponent{})) ==
          prefix(scene, child) +
              "parent component: entity is the null entity ID, must name the parent");
}

TEST_CASE("A parent is another entity of the same scene", "[scene][parent]")
{
    Scene scene = makeScene();
    Scene other = makeScene("Other", 22);
    const EntityId parent = makeEntity(scene);
    const EntityId child = makeEntity(scene);
    const EntityId elsewhere = makeEntity(other);

    CHECK(messageOf(scene.addComponent(child, ParentComponent{child})) ==
          prefix(scene, child) + "parent component: an entity cannot be its own parent");
    CHECK(messageOf(scene.addComponent(child, ParentComponent{elsewhere})) ==
          prefix(scene, child) + "parent component: parent " + elsewhere.toString() +
              " does not exist in scene 'Main'");
    CHECK_FALSE(scene.hasComponent<ParentComponent>(child));

    REQUIRE_FALSE(scene.addComponent(child, ParentComponent{parent}).has_value());
    CHECK(scene.component<ParentComponent>(child).value().entity == parent);
    CHECK(scene.entitiesWith<ParentComponent>() == std::vector<EntityId>{child});
}

TEST_CASE("Parent links never form a cycle", "[scene][parent]")
{
    Scene scene = makeScene();
    const EntityId a = makeEntity(scene);
    const EntityId b = makeEntity(scene);
    const EntityId c = makeEntity(scene);
    REQUIRE_FALSE(scene.addComponent(b, ParentComponent{a}).has_value());
    REQUIRE_FALSE(scene.addComponent(c, ParentComponent{b}).has_value());

    CHECK(messageOf(scene.addComponent(a, ParentComponent{b})) ==
          prefix(scene, a) + "parent component: parent " + b.toString() +
              " is a descendant of the entity, which would make a cycle");
    CHECK(messageOf(scene.addComponent(a, ParentComponent{c})).ends_with("would make a cycle"));
    // Replacing a link is checked the same way.
    CHECK(messageOf(scene.replaceComponent(b, ParentComponent{c})).ends_with("would make a cycle"));
    CHECK(scene.component<ParentComponent>(b).value().entity == a);
    // Moving a subtree elsewhere is fine.
    const EntityId d = makeEntity(scene);
    REQUIRE_FALSE(scene.replaceComponent(b, ParentComponent{d}).has_value());
    REQUIRE_FALSE(scene.addComponent(a, ParentComponent{c}).has_value());
}

TEST_CASE("Destroying a parent detaches its children", "[scene][parent]")
{
    Scene scene = makeScene();
    const EntityId parent = makeEntity(scene);
    const EntityId first = makeEntity(scene);
    const EntityId second = makeEntity(scene);
    const EntityId grandchild = makeEntity(scene);
    const EntityId unrelated = makeEntity(scene);
    REQUIRE_FALSE(scene.addComponent(first, ParentComponent{parent}).has_value());
    REQUIRE_FALSE(scene.addComponent(first, NameComponent{"first"}).has_value());
    REQUIRE_FALSE(scene.addComponent(second, ParentComponent{parent}).has_value());
    REQUIRE_FALSE(scene.addComponent(second, TransformComponent{}).has_value());
    REQUIRE_FALSE(scene.addComponent(grandchild, ParentComponent{first}).has_value());
    REQUIRE_FALSE(scene.addComponent(unrelated, ParentComponent{grandchild}).has_value());

    REQUIRE_FALSE(scene.destroyEntity(parent).has_value());
    // The children stay, with their IDs and other components.
    CHECK(scene.entityIds() == std::vector<EntityId>{first, second, grandchild, unrelated});
    CHECK_FALSE(scene.hasComponent<ParentComponent>(first));
    CHECK_FALSE(scene.hasComponent<ParentComponent>(second));
    CHECK(scene.component<NameComponent>(first).value() == NameComponent{"first"});
    CHECK(scene.hasComponent<TransformComponent>(second));
    // Links further down are untouched.
    CHECK(scene.component<ParentComponent>(grandchild).value().entity == first);
    CHECK(scene.component<ParentComponent>(unrelated).value().entity == grandchild);

    // An entity created again with the parent's ID gets no children back.
    REQUIRE(scene.createEntity(parent));
    CHECK(scene.entitiesWith<ParentComponent>() == std::vector<EntityId>{grandchild, unrelated});
}

TEST_CASE("Scene documents keep parent links", "[scene][parent][document]")
{
    Scene scene = makeScene();
    const EntityId child = makeEntity(scene);
    const EntityId parent = makeEntity(scene);
    // The child comes before its parent in the document.
    REQUIRE_FALSE(scene.addComponent(child, ParentComponent{parent}).has_value());
    const std::string text = engine::scene::sceneToJson(scene);
    CHECK(text.find("\"parent\": {\n          \"entity\": \"" + parent.toString() + "\"\n") !=
          std::string::npos);
    auto loaded = engine::scene::sceneFromJson(text, UuidGenerator(1));
    REQUIRE(loaded);
    CHECK(loaded.value().scene.component<ParentComponent>(child).value().entity == parent);
    CHECK(engine::scene::sceneToJson(loaded.value().scene) == text);
}

TEST_CASE("Scene documents reject broken parent links with their path", "[scene][parent][document]")
{
    const std::string a = "b3f2af6d-0fc7-40c5-853b-559647364cea";
    const std::string b = "92f89756-082a-4514-a42e-1c7bc266a3a7";
    const std::string c = "b27a48e2-9a23-4673-a4c1-23126ffda722";
    const auto entity = [](const std::string& id, const std::string& parent) {
        return "{\"id\": \"" + id + "\", \"components\": {\"parent\": {\"entity\": \"" + parent +
               "\"}}}";
    };
    const auto document = [](const std::string& entities) {
        return R"({"format": "lpe.scene", "formatVersion": 1, "id": "01234567-89ab-cdef-8000-000000000001", "name": "Main", "entities": [)" +
               entities + "]}";
    };
    const auto errorOf = [](const std::string& text) {
        const auto loaded = engine::scene::sceneFromJson(text, UuidGenerator(1));
        REQUIRE_FALSE(loaded);
        return loaded.error().message;
    };

    CHECK(errorOf(document(entity(a, a))) ==
          "entities[0].components.parent.entity: an entity cannot be its own parent");
    CHECK(errorOf(document(entity(a, b))) ==
          "entities[0].components.parent.entity: entity " + b + " is not in the document");
    CHECK(errorOf(document(entity(a, b) + ", " + entity(b, c) + ", " + entity(c, a))) ==
          "entities[0].components.parent.entity: entity " + b +
              " is a descendant of this entity, which makes a cycle");
    CHECK(errorOf(document("{\"id\": \"" + a + "\", \"components\": {\"parent\": {}}}")) ==
          "entities[0].components.parent.entity: missing");
    CHECK(errorOf(
              document("{\"id\": \"" + a + "\", \"components\": {\"parent\": {\"entity\": 3}}}")) ==
          "entities[0].components.parent.entity: expected a string, found number 3");
}
