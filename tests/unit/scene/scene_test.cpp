#include "engine/scene/EntityId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneId.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using engine::core::Uuid;
using engine::core::UuidGenerator;
using engine::scene::EntityId;
using engine::scene::Scene;
using engine::scene::SceneId;
using engine::scene::validateName;

namespace {

// Golden IDs of UuidGenerator(1) (tests/unit/core/uuid_test.cpp, ADR 0020).
const EntityId kSeed1First{Uuid{0xB3F2AF6D0FC740C5ULL, 0x853B559647364CEAULL}};
const EntityId kSeed1Second{Uuid{0x92F89756082A4514ULL, 0xA42E1C7BC266A3A7ULL}};
const EntityId kSeed1Third{Uuid{0xB27A48E29A234673ULL, 0xA4C123126FFDA722ULL}};

const SceneId kMainId{Uuid{0x0123456789ABCDEFULL, 0x8000000000000001ULL}};

Scene makeScene(std::string_view name = "Main", std::uint64_t seed = 1)
{
    auto created = Scene::create(kMainId, name, UuidGenerator(seed));
    REQUIRE(created);
    return std::move(created).value();
}

EntityId makeEntity(Scene& scene)
{
    auto created = scene.createEntity();
    REQUIRE(created);
    return created.value();
}

std::string errorOf(const std::optional<engine::core::Error>& error)
{
    REQUIRE(error.has_value());
    return error->message;
}

} // namespace

TEST_CASE("Entity and scene IDs are distinct types without conversions", "[scene][id]")
{
    static_assert(!std::is_convertible_v<EntityId, SceneId>);
    static_assert(!std::is_constructible_v<SceneId, EntityId>);
    static_assert(!std::is_constructible_v<EntityId, SceneId>);
    static_assert(!std::is_convertible_v<Uuid, EntityId>);
    static_assert(!std::is_constructible_v<EntityId, std::uint64_t>);
    static_assert(!std::is_constructible_v<EntityId, const void*>);
    static_assert(!std::is_constructible_v<EntityId, const Scene*>);

    const auto entity = EntityId::parse("b3f2af6d-0fc7-40c5-853b-559647364cea");
    REQUIRE(entity);
    CHECK(entity.value() == kSeed1First);
    CHECK(EntityId::parse(kSeed1Second.toString()).value() == kSeed1Second);

    const auto bad = EntityId::parse("island-7");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.starts_with("entity ID 'island-7': expected 36 characters"));
    const auto null = SceneId::parse("00000000-0000-0000-0000-000000000000");
    REQUIRE_FALSE(null);
    CHECK(null.error().message ==
          "scene ID '00000000-0000-0000-0000-000000000000': the null ID is not an identity");
}

TEST_CASE("A scene has its ID, its name and no entities", "[scene]")
{
    const Scene scene = makeScene();
    CHECK(scene.id() == kMainId);
    CHECK(scene.name() == "Main");
    CHECK(scene.entityCount() == 0);
    CHECK(scene.entityIds().empty());
}

TEST_CASE("A scene without a given ID draws one from its generator", "[scene]")
{
    auto created = Scene::create("Main", UuidGenerator(1));
    REQUIRE(created);
    Scene scene = std::move(created).value();
    CHECK(scene.id().value() == kSeed1First.value());
    // The generator goes on with the entities.
    const auto entity = scene.createEntity();
    REQUIRE(entity);
    CHECK(entity.value() == kSeed1Second);
}

TEST_CASE("A scene needs a valid ID and name", "[scene]")
{
    const auto nullId = Scene::create(SceneId{}, "Main", UuidGenerator(1));
    REQUIRE_FALSE(nullId);
    CHECK(nullId.error().message == "scene: the null scene ID is not an identity");

    const auto empty = Scene::create(kMainId, "", UuidGenerator(1));
    REQUIRE_FALSE(empty);
    CHECK(empty.error().message == "scene name: must not be empty");

    const auto russian = Scene::create(kMainId, "Острова 1", UuidGenerator(1));
    REQUIRE(russian);
    CHECK(russian.value().name() == "Острова 1");
}

TEST_CASE("Names are checked for length, UTF-8 and control characters", "[scene][name]")
{
    CHECK_FALSE(validateName("Main", "scene name").has_value());
    CHECK_FALSE(validateName("Остров α 日本", "scene name").has_value());
    CHECK_FALSE(validateName(std::string(256, 'n'), "scene name").has_value());

    CHECK(errorOf(validateName("", "scene name")) == "scene name: must not be empty");
    CHECK(errorOf(validateName(std::string(257, 'n'), "scene name")) ==
          "scene name: 257 bytes, at most 256 allowed");
    CHECK(errorOf(validateName("ab\xC3", "scene name")) == "scene name: invalid UTF-8 at byte 2");
    CHECK(errorOf(validateName("\xC0\xAF", "scene name")) == "scene name: invalid UTF-8 at byte 0");
    CHECK(errorOf(validateName("a\tb", "scene name")) ==
          "scene name: control character U+0009 at byte 1");
    CHECK(errorOf(validateName("line\n", "scene name")) ==
          "scene name: control character U+000A at byte 4");
    CHECK(errorOf(validateName("del\x7F", "scene name")) ==
          "scene name: control character U+007F at byte 3");
    CHECK(errorOf(validateName("next\xC2\x85line", "scene name")) ==
          "scene name: control character U+0085 at byte 4");
    CHECK(errorOf(validateName(std::string_view("a\0b", 3), "scene name")) ==
          "scene name: control character U+0000 at byte 1");
}

TEST_CASE("Entities are created, found and destroyed", "[scene]")
{
    Scene scene = makeScene();
    const EntityId entity = makeEntity(scene);
    CHECK(entity == kSeed1First);
    CHECK(scene.contains(entity));
    CHECK_FALSE(scene.findEntity(entity).has_value());
    CHECK(scene.entityCount() == 1);
    CHECK(scene.entityIds() == std::vector<EntityId>{entity});

    REQUIRE_FALSE(scene.destroyEntity(entity).has_value());
    CHECK_FALSE(scene.contains(entity));
    CHECK(scene.entityCount() == 0);
    const std::string missing =
        "scene 'Main': entity b3f2af6d-0fc7-40c5-853b-559647364cea: does not exist";
    CHECK(errorOf(scene.findEntity(entity)) == missing);
    CHECK(errorOf(scene.destroyEntity(entity)) == missing);
}

TEST_CASE("Unknown and null entity IDs are reported", "[scene]")
{
    Scene scene = makeScene();
    (void)makeEntity(scene);
    CHECK_FALSE(scene.contains(kSeed1Third));
    CHECK(errorOf(scene.findEntity(kSeed1Third)) ==
          "scene 'Main': entity b27a48e2-9a23-4673-a4c1-23126ffda722: does not exist");
    CHECK(errorOf(scene.destroyEntity(kSeed1Third)) ==
          "scene 'Main': entity b27a48e2-9a23-4673-a4c1-23126ffda722: does not exist");
    CHECK_FALSE(scene.contains(EntityId{}));
    CHECK(errorOf(scene.findEntity(EntityId{})) ==
          "scene 'Main': the null entity ID is not an identity");
    CHECK(errorOf(scene.destroyEntity(EntityId{})) ==
          "scene 'Main': the null entity ID is not an identity");
    CHECK(scene.entityCount() == 1);
}

TEST_CASE("Entities can be created with a given ID, once", "[scene]")
{
    Scene scene = makeScene();
    const auto created = scene.createEntity(kSeed1Third);
    REQUIRE(created);
    CHECK(created.value() == kSeed1Third);

    const auto again = scene.createEntity(kSeed1Third);
    REQUIRE_FALSE(again);
    CHECK(again.error().message ==
          "scene 'Main': entity b27a48e2-9a23-4673-a4c1-23126ffda722: already exists");

    const auto null = scene.createEntity(EntityId{});
    REQUIRE_FALSE(null);
    CHECK(null.error().message == "scene 'Main': the null entity ID is not an identity");
    CHECK(scene.entityCount() == 1);
}

TEST_CASE("A generated ID that is already taken is drawn again", "[scene]")
{
    // The scene's generator would give kSeed1First next; it is taken.
    auto created = Scene::create(kMainId, "Main", UuidGenerator(1));
    REQUIRE(created);
    Scene scene = std::move(created).value();
    REQUIRE(scene.createEntity(kSeed1First));
    const EntityId generated = makeEntity(scene);
    CHECK(generated == kSeed1Second);
    CHECK(scene.entityCount() == 2);
}

TEST_CASE("A destroyed entity's ID comes back only by explicit creation", "[scene]")
{
    Scene scene = makeScene();
    const EntityId a = makeEntity(scene);
    const EntityId b = makeEntity(scene);
    REQUIRE_FALSE(scene.destroyEntity(a).has_value());
    CHECK(scene.findEntity(a).has_value());

    // Generated IDs never repeat it.
    std::set<EntityId> generated;
    for (int i = 0; i < 1000; ++i) {
        generated.insert(makeEntity(scene));
    }
    CHECK(generated.count(a) == 0);

    // Created again with its ID, it goes to the end of the order.
    REQUIRE(scene.createEntity(a));
    const std::vector<EntityId> ids = scene.entityIds();
    REQUIRE(ids.size() == 1002);
    CHECK(ids.front() == b);
    CHECK(ids.back() == a);
}

TEST_CASE("Entity IDs are listed in creation order", "[scene]")
{
    Scene scene = makeScene();
    std::vector<EntityId> created;
    for (int i = 0; i < 5; ++i) {
        created.push_back(makeEntity(scene));
    }
    REQUIRE_FALSE(scene.destroyEntity(created[1]).has_value());
    REQUIRE_FALSE(scene.destroyEntity(created[3]).has_value());
    const EntityId late = makeEntity(scene);
    CHECK(scene.entityIds() == std::vector<EntityId>{created[0], created[2], created[4], late});
}

TEST_CASE("Clearing a scene keeps its ID and name", "[scene]")
{
    Scene scene = makeScene();
    const EntityId a = makeEntity(scene);
    const EntityId b = makeEntity(scene);
    scene.clear();
    CHECK(scene.entityCount() == 0);
    CHECK(scene.entityIds().empty());
    CHECK_FALSE(scene.contains(a));
    CHECK_FALSE(scene.contains(b));
    CHECK(scene.findEntity(a).has_value());
    CHECK(scene.id() == kMainId);
    CHECK(scene.name() == "Main");

    // The scene is usable afterwards, and old IDs can be given again.
    REQUIRE(scene.createEntity(b));
    const EntityId c = makeEntity(scene);
    CHECK(c != a);
    CHECK(scene.entityIds() == std::vector<EntityId>{b, c});
}

TEST_CASE("Moving a scene moves its entities and empties the source", "[scene]")
{
    Scene source = makeScene("Source");
    const EntityId a = makeEntity(source);
    const EntityId b = makeEntity(source);

    Scene moved(std::move(source));
    CHECK(moved.id() == kMainId);
    CHECK(moved.name() == "Source");
    CHECK(moved.entityIds() == std::vector<EntityId>{a, b});
    // NOLINTBEGIN(bugprone-use-after-move): the moved-from state is documented.
    CHECK(source.id().isNull());
    CHECK(source.name().empty());
    CHECK(source.entityCount() == 0);
    CHECK_FALSE(source.contains(a));
    // NOLINTEND(bugprone-use-after-move)

    Scene target = makeScene("Target");
    (void)makeEntity(target);
    target = std::move(moved);
    CHECK(target.name() == "Source");
    CHECK(target.entityIds() == std::vector<EntityId>{a, b});
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK(moved.entityCount() == 0);

    // The generator moved with the scene: new IDs do not repeat old ones.
    const EntityId c = makeEntity(target);
    CHECK(c != a);
    CHECK(c != b);
}

TEST_CASE("The same ID may exist in two scenes independently", "[scene]")
{
    Scene first = makeScene("First");
    Scene second = makeScene("Second");
    REQUIRE(first.createEntity(kSeed1Third));
    REQUIRE(second.createEntity(kSeed1Third));
    REQUIRE_FALSE(first.destroyEntity(kSeed1Third).has_value());
    CHECK_FALSE(first.contains(kSeed1Third));
    CHECK(second.contains(kSeed1Third));
}

TEST_CASE("A scene holds many entities", "[scene]")
{
    Scene scene = makeScene("Large", 12345);
    constexpr std::size_t kCount = 100000;
    for (std::size_t i = 0; i < kCount; ++i) {
        REQUIRE(scene.createEntity());
    }
    const std::vector<EntityId> all = scene.entityIds();
    REQUIRE(all.size() == kCount);
    CHECK(std::set<EntityId>(all.begin(), all.end()).size() == kCount);

    std::vector<EntityId> kept;
    for (std::size_t i = 0; i < kCount; ++i) {
        if (i % 2 == 0) {
            REQUIRE_FALSE(scene.destroyEntity(all[i]).has_value());
        } else {
            kept.push_back(all[i]);
        }
    }
    CHECK(scene.entityCount() == kCount / 2);
    CHECK(scene.entityIds() == kept);

    for (const EntityId& entity : kept) {
        REQUIRE_FALSE(scene.destroyEntity(entity).has_value());
    }
    CHECK(scene.entityCount() == 0);
}
