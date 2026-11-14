#include "engine/scene/Scene.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using engine::core::Uuid;
using engine::core::UuidGenerator;
using engine::math::Quat;
using engine::math::Vec3;
using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::Scene;
using engine::scene::SceneId;
using engine::scene::ScriptComponent;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;

namespace {

const SceneId kMainId{Uuid{0x0123456789ABCDEFULL, 0x8000000000000001ULL}};
const EntityId kUnknown{Uuid{0x00000000DEADBEEFULL, 0x8000000000000002ULL}};

Scene makeScene(std::string_view name = "Main")
{
    auto created = Scene::create(kMainId, name, UuidGenerator(7));
    REQUIRE(created);
    return std::move(created).value();
}

EntityId makeEntity(Scene& scene)
{
    auto created = scene.createEntity();
    REQUIRE(created);
    return created.value();
}

std::string prefix(const Scene& scene, EntityId entity)
{
    return "scene '" + scene.name() + "': entity " + entity.toString() + ": ";
}

// The message of an operation that must have failed.
std::string messageOf(const std::optional<engine::core::Error>& error)
{
    REQUIRE(error.has_value());
    return error->message;
}

// Two different valid values and one invalid value per component type, and
// the message of the invalid one.
template <typename T>
struct Samples;

template <>
struct Samples<NameComponent> {
    static NameComponent first() { return {"Lighthouse"}; }
    static NameComponent second() { return {"Маяк"}; }
    static NameComponent invalid() { return {""}; }
    static constexpr std::string_view kInvalidMessage = "name component: value: must not be empty";
};

template <>
struct Samples<TransformComponent> {
    static TransformComponent first()
    {
        return {{1.0F, 2.0F, 3.0F}, Quat{1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}};
    }
    static TransformComponent second()
    {
        return {{-4.5F, 0.0F, 9.0F}, Quat{0.0F, 0.0F, 1.0F, 0.0F}, {2.0F, 0.5F, 3.0F}};
    }
    static TransformComponent invalid()
    {
        TransformComponent transform;
        transform.scale.y = 0.0F;
        return transform;
    }
    static constexpr std::string_view kInvalidMessage =
        "transform component: scale.y is 0, must be in [0.0001, 10000]";
};

template <>
struct Samples<StaticMeshComponent> {
    static engine::assets::AssetId asset(std::uint64_t number)
    {
        return engine::assets::AssetId{Uuid{0x5555555555554555ULL, 0x8000000000000000ULL | number}};
    }
    static StaticMeshComponent first() { return {asset(1), {}}; }
    static StaticMeshComponent second() { return {asset(2), asset(3)}; }
    static StaticMeshComponent invalid() { return {{}, asset(3)}; }
    static constexpr std::string_view kInvalidMessage =
        "static mesh component: mesh is the null asset ID, must name a mesh asset";
};

template <>
struct Samples<CameraComponent> {
    static CameraComponent first() { return {1.0F, 0.1F, 500.0F}; }
    static CameraComponent second() { return {0.5F, 2.0F, 10000.0F}; }
    static CameraComponent invalid() { return {1.0F, 0.0F, 500.0F}; }
    static constexpr std::string_view kInvalidMessage =
        "camera component: nearPlane is 0, must be at least 0.001";
};

template <>
struct Samples<DirectionalLightComponent> {
    static DirectionalLightComponent first()
    {
        return {{0.0F, -1.0F, 0.0F}, {1.0F, 0.9F, 0.8F}, 2.0F};
    }
    static DirectionalLightComponent second()
    {
        return {{1.0F, 0.0F, 0.0F}, {0.2F, 0.3F, 1.0F}, 0.5F};
    }
    static DirectionalLightComponent invalid()
    {
        return {{0.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, -1.0F};
    }
    static constexpr std::string_view kInvalidMessage =
        "directional light component: intensity is -1, must be at least 0";
};

template <>
struct Samples<ScriptComponent> {
    static engine::assets::AssetId asset(std::uint64_t number)
    {
        return engine::assets::AssetId{Uuid{0x5C21975C21974C21ULL, 0x8000000000000000ULL | number}};
    }
    static ScriptComponent first() { return {asset(1), true}; }
    static ScriptComponent second() { return {asset(2), false}; }
    static ScriptComponent invalid() { return {{}, true}; }
    static constexpr std::string_view kInvalidMessage =
        "script component: asset is the null asset ID, must name a script asset";
};

template <typename T>
std::string typeName()
{
    return std::string(T::kTypeName);
}

bool sameBits(const Quat& a, const Quat& b)
{
    return std::bit_cast<std::uint32_t>(a.w) == std::bit_cast<std::uint32_t>(b.w) &&
           std::bit_cast<std::uint32_t>(a.x) == std::bit_cast<std::uint32_t>(b.x) &&
           std::bit_cast<std::uint32_t>(a.y) == std::bit_cast<std::uint32_t>(b.y) &&
           std::bit_cast<std::uint32_t>(a.z) == std::bit_cast<std::uint32_t>(b.z);
}

bool sameBits(const Vec3& a, const Vec3& b)
{
    return std::bit_cast<std::uint32_t>(a.x) == std::bit_cast<std::uint32_t>(b.x) &&
           std::bit_cast<std::uint32_t>(a.y) == std::bit_cast<std::uint32_t>(b.y) &&
           std::bit_cast<std::uint32_t>(a.z) == std::bit_cast<std::uint32_t>(b.z);
}

double lengthOf(const Quat& q)
{
    const auto w = static_cast<double>(q.w);
    const auto x = static_cast<double>(q.x);
    const auto y = static_cast<double>(q.y);
    const auto z = static_cast<double>(q.z);
    return std::sqrt((w * w) + (x * x) + (y * y) + (z * z));
}

double lengthOf(const Vec3& v)
{
    const auto x = static_cast<double>(v.x);
    const auto y = static_cast<double>(v.y);
    const auto z = static_cast<double>(v.z);
    return std::sqrt((x * x) + (y * y) + (z * z));
}

} // namespace

using AllComponents = std::tuple<NameComponent, TransformComponent, StaticMeshComponent,
                                 CameraComponent, DirectionalLightComponent, ScriptComponent>;

TEMPLATE_LIST_TEST_CASE("A component is added, read, replaced and removed", "[scene][components]",
                        AllComponents)
{
    Scene scene = makeScene();
    const EntityId entity = makeEntity(scene);
    CHECK_FALSE(scene.hasComponent<TestType>(entity));

    REQUIRE_FALSE(scene.addComponent(entity, Samples<TestType>::first()).has_value());
    CHECK(scene.hasComponent<TestType>(entity));
    auto read = scene.component<TestType>(entity);
    REQUIRE(read);
    CHECK(read.value() == Samples<TestType>::first());

    REQUIRE_FALSE(scene.replaceComponent(entity, Samples<TestType>::second()).has_value());
    read = scene.component<TestType>(entity);
    REQUIRE(read);
    CHECK(read.value() == Samples<TestType>::second());

    REQUIRE_FALSE(scene.removeComponent<TestType>(entity).has_value());
    CHECK_FALSE(scene.hasComponent<TestType>(entity));
    CHECK(scene.contains(entity));
    const auto missing = scene.component<TestType>(entity);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message ==
          prefix(scene, entity) + "has no " + typeName<TestType>() + " component");

    // The entity's identity does not change while components come and go.
    CHECK(scene.entityIds() == std::vector<EntityId>{entity});
}

TEMPLATE_LIST_TEST_CASE("A component is added at most once and changed only when present",
                        "[scene][components]", AllComponents)
{
    Scene scene = makeScene();
    const EntityId entity = makeEntity(scene);

    const auto replaced = scene.replaceComponent(entity, Samples<TestType>::first());
    REQUIRE(replaced.has_value());
    CHECK(replaced->message ==
          prefix(scene, entity) + "has no " + typeName<TestType>() + " component");
    CHECK_FALSE(scene.hasComponent<TestType>(entity));

    const auto removed = scene.removeComponent<TestType>(entity);
    REQUIRE(removed.has_value());
    CHECK(removed->message == replaced->message);

    REQUIRE_FALSE(scene.addComponent(entity, Samples<TestType>::first()).has_value());
    const auto twice = scene.addComponent(entity, Samples<TestType>::second());
    REQUIRE(twice.has_value());
    CHECK(twice->message ==
          prefix(scene, entity) + "already has a " + typeName<TestType>() + " component");
    CHECK(scene.component<TestType>(entity).value() == Samples<TestType>::first());
}

TEMPLATE_LIST_TEST_CASE("An invalid component value is rejected and changes nothing",
                        "[scene][components]", AllComponents)
{
    Scene scene = makeScene();
    const EntityId entity = makeEntity(scene);
    const std::string expected =
        prefix(scene, entity) + std::string(Samples<TestType>::kInvalidMessage);

    const auto added = scene.addComponent(entity, Samples<TestType>::invalid());
    REQUIRE(added.has_value());
    CHECK(added->message == expected);
    CHECK_FALSE(scene.hasComponent<TestType>(entity));

    REQUIRE_FALSE(scene.addComponent(entity, Samples<TestType>::first()).has_value());
    const auto replaced = scene.replaceComponent(entity, Samples<TestType>::invalid());
    REQUIRE(replaced.has_value());
    CHECK(replaced->message == expected);
    CHECK(scene.component<TestType>(entity).value() == Samples<TestType>::first());
}

TEMPLATE_LIST_TEST_CASE("Component operations fail for unknown, null and destroyed entities",
                        "[scene][components]", AllComponents)
{
    Scene scene = makeScene();
    const EntityId destroyed = makeEntity(scene);
    REQUIRE_FALSE(scene.addComponent(destroyed, Samples<TestType>::first()).has_value());
    REQUIRE_FALSE(scene.destroyEntity(destroyed).has_value());

    for (const EntityId entity : {kUnknown, destroyed}) {
        const std::string expected = prefix(scene, entity) + "does not exist";
        CHECK(messageOf(scene.addComponent(entity, Samples<TestType>::first())) == expected);
        CHECK(messageOf(scene.replaceComponent(entity, Samples<TestType>::first())) == expected);
        CHECK(messageOf(scene.removeComponent<TestType>(entity)) == expected);
        CHECK(scene.component<TestType>(entity).error().message == expected);
        CHECK_FALSE(scene.hasComponent<TestType>(entity));
    }
    const std::string null = "scene 'Main': the null entity ID is not an identity";
    CHECK(messageOf(scene.addComponent(EntityId{}, Samples<TestType>::first())) == null);
    CHECK(messageOf(scene.replaceComponent(EntityId{}, Samples<TestType>::first())) == null);
    CHECK(messageOf(scene.removeComponent<TestType>(EntityId{})) == null);
    CHECK(scene.component<TestType>(EntityId{}).error().message == null);
    CHECK_FALSE(scene.hasComponent<TestType>(EntityId{}));
    CHECK(scene.entitiesWith<TestType>().empty());
}

TEST_CASE("An entity holds several components independently", "[scene][components]")
{
    Scene scene = makeScene();
    const EntityId entity = makeEntity(scene);
    REQUIRE_FALSE(scene.addComponent(entity, NameComponent{"Sun"}).has_value());
    REQUIRE_FALSE(scene.addComponent(entity, Samples<TransformComponent>::first()).has_value());
    REQUIRE_FALSE(
        scene.addComponent(entity, Samples<DirectionalLightComponent>::first()).has_value());

    CHECK(scene.hasComponent<NameComponent>(entity));
    CHECK(scene.hasComponent<TransformComponent>(entity));
    CHECK_FALSE(scene.hasComponent<CameraComponent>(entity));
    CHECK(scene.hasComponent<DirectionalLightComponent>(entity));

    REQUIRE_FALSE(scene.removeComponent<TransformComponent>(entity).has_value());
    CHECK(scene.component<NameComponent>(entity).value() == NameComponent{"Sun"});
    CHECK(scene.component<DirectionalLightComponent>(entity).value() ==
          Samples<DirectionalLightComponent>::first());
    CHECK_FALSE(scene.hasComponent<TransformComponent>(entity));
}

TEST_CASE("Queries return entities in creation order", "[scene][components]")
{
    Scene scene = makeScene();
    std::vector<EntityId> entities;
    for (int i = 0; i < 8; ++i) {
        entities.push_back(makeEntity(scene));
    }
    // Added out of creation order, so EnTT's pools hold another order.
    for (const std::size_t index : {6U, 1U, 4U, 0U, 3U}) {
        REQUIRE_FALSE(
            scene.addComponent(entities[index], Samples<TransformComponent>::first()).has_value());
    }
    for (const std::size_t index : {4U, 7U, 0U, 6U}) {
        REQUIRE_FALSE(scene.addComponent(entities[index], NameComponent{"n"}).has_value());
    }

    CHECK(scene.entitiesWith<TransformComponent>() ==
          std::vector<EntityId>{entities[0], entities[1], entities[3], entities[4], entities[6]});
    CHECK(scene.entitiesWith<NameComponent>() ==
          std::vector<EntityId>{entities[0], entities[4], entities[6], entities[7]});
    CHECK(scene.entitiesWith<TransformComponent, NameComponent>() ==
          std::vector<EntityId>{entities[0], entities[4], entities[6]});
    CHECK(scene.entitiesWith<NameComponent, TransformComponent>() ==
          std::vector<EntityId>{entities[0], entities[4], entities[6]});
    CHECK(scene.entitiesWith<CameraComponent>().empty());
    CHECK(scene.entitiesWith<NameComponent, CameraComponent>().empty());

    // Removing components and destroying entities keeps the order; an ID
    // created again goes to the end and starts without components.
    REQUIRE_FALSE(scene.removeComponent<NameComponent>(entities[4]).has_value());
    REQUIRE_FALSE(scene.destroyEntity(entities[0]).has_value());
    REQUIRE(scene.createEntity(entities[0]));
    CHECK_FALSE(scene.hasComponent<TransformComponent>(entities[0]));
    CHECK_FALSE(scene.hasComponent<NameComponent>(entities[0]));
    REQUIRE_FALSE(scene.addComponent(entities[0], NameComponent{"again"}).has_value());
    REQUIRE_FALSE(
        scene.addComponent(entities[0], Samples<TransformComponent>::second()).has_value());
    CHECK(scene.entitiesWith<TransformComponent, NameComponent>() ==
          std::vector<EntityId>{entities[6], entities[0]});
    CHECK(scene.entitiesWith<TransformComponent>() ==
          std::vector<EntityId>{entities[1], entities[3], entities[4], entities[6], entities[0]});
}

TEST_CASE("Destroying an entity or clearing the scene removes its components",
          "[scene][components]")
{
    Scene scene = makeScene();
    const EntityId a = makeEntity(scene);
    const EntityId b = makeEntity(scene);
    REQUIRE_FALSE(scene.addComponent(a, NameComponent{"a"}).has_value());
    REQUIRE_FALSE(scene.addComponent(a, CameraComponent{}).has_value());
    REQUIRE_FALSE(scene.addComponent(b, CameraComponent{}).has_value());

    REQUIRE_FALSE(scene.destroyEntity(a).has_value());
    CHECK(scene.entitiesWith<CameraComponent>() == std::vector<EntityId>{b});
    CHECK(scene.entitiesWith<NameComponent>().empty());
    REQUIRE(scene.createEntity(a));
    CHECK_FALSE(scene.hasComponent<NameComponent>(a));
    CHECK_FALSE(scene.hasComponent<CameraComponent>(a));

    scene.clear();
    CHECK(scene.entitiesWith<CameraComponent>().empty());
    REQUIRE(scene.createEntity(b));
    CHECK_FALSE(scene.hasComponent<CameraComponent>(b));
    CHECK(scene.id() == kMainId);
}

TEST_CASE("Moving a scene moves its components", "[scene][components]")
{
    Scene source = makeScene();
    const EntityId entity = makeEntity(source);
    REQUIRE_FALSE(source.addComponent(entity, NameComponent{"moved"}).has_value());

    Scene moved(std::move(source));
    CHECK(moved.component<NameComponent>(entity).value() == NameComponent{"moved"});
    // NOLINTBEGIN(bugprone-use-after-move): the moved-from state is documented.
    CHECK_FALSE(source.hasComponent<NameComponent>(entity));
    CHECK(source.entitiesWith<NameComponent>().empty());
    CHECK(source.entityIds().empty());
    // A moved-from scene can be used again.
    REQUIRE(source.createEntity(entity));
    CHECK_FALSE(source.hasComponent<NameComponent>(entity));
    // NOLINTEND(bugprone-use-after-move)
}

TEST_CASE("Rotations are stored normalized, unit rotations bit for bit", "[scene][components]")
{
    Scene scene = makeScene();
    const EntityId entity = makeEntity(scene);

    TransformComponent transform;
    transform.rotation = Quat{1.0008F, 0.0F, 0.0F, 0.0F};
    REQUIRE_FALSE(scene.addComponent(entity, transform).has_value());
    CHECK(scene.component<TransformComponent>(entity).value().rotation ==
          Quat{1.0F, 0.0F, 0.0F, 0.0F});

    // Already unit within 1e-6: kept exactly.
    transform.rotation = Quat{0.70710677F, 0.0F, 0.70710677F, 0.0F};
    REQUIRE_FALSE(scene.replaceComponent(entity, transform).has_value());
    CHECK(
        sameBits(scene.component<TransformComponent>(entity).value().rotation, transform.rotation));

    // Random rotations up to the tolerance: the stored rotation has unit
    // length, points the same way, and storing it again changes no bit (a
    // saved scene reads back unchanged).
    std::mt19937 random(12345U);
    std::uniform_real_distribution<float> component(-1.0F, 1.0F);
    std::uniform_real_distribution<float> scale(0.9991F, 1.0009F);
    for (int i = 0; i < 10000; ++i) {
        Quat direction{component(random), component(random), component(random), component(random)};
        const double length = lengthOf(direction);
        if (length < 0.1) {
            continue;
        }
        const float factor = scale(random) / static_cast<float>(length);
        transform.rotation = Quat{direction.w * factor, direction.x * factor, direction.y * factor,
                                  direction.z * factor};
        if (std::abs(lengthOf(transform.rotation) - 1.0) > 0.001) {
            continue;
        }
        REQUIRE_FALSE(scene.replaceComponent(entity, transform).has_value());
        const Quat stored = scene.component<TransformComponent>(entity).value().rotation;
        CHECK(std::abs(lengthOf(stored) - 1.0) <= 1.0e-6);
        CHECK(std::abs(static_cast<double>(stored.w) - static_cast<double>(transform.rotation.w) /
                                                           lengthOf(transform.rotation)) < 1.0e-6);

        TransformComponent again = transform;
        again.rotation = stored;
        REQUIRE_FALSE(scene.replaceComponent(entity, again).has_value());
        CHECK(sameBits(scene.component<TransformComponent>(entity).value().rotation, stored));
    }
}

TEST_CASE("Light directions are stored normalized", "[scene][components]")
{
    Scene scene = makeScene();
    const EntityId entity = makeEntity(scene);

    DirectionalLightComponent light;
    light.direction = {0.0F, -5.0F, 0.0F};
    REQUIRE_FALSE(scene.addComponent(entity, light).has_value());
    CHECK(scene.component<DirectionalLightComponent>(entity).value().direction ==
          Vec3{0.0F, -1.0F, 0.0F});

    light.direction = {0.0F, 3.0e-30F, 0.0F};
    REQUIRE_FALSE(scene.replaceComponent(entity, light).has_value());
    CHECK(scene.component<DirectionalLightComponent>(entity).value().direction ==
          Vec3{0.0F, 1.0F, 0.0F});

    light.direction = {3.0e38F, 0.0F, -3.0e38F};
    REQUIRE_FALSE(scene.replaceComponent(entity, light).has_value());
    const Vec3 diagonal = scene.component<DirectionalLightComponent>(entity).value().direction;
    CHECK(std::abs(lengthOf(diagonal) - 1.0) <= 1.0e-6);
    CHECK(diagonal.x == -diagonal.z);

    std::mt19937 random(777U);
    std::uniform_real_distribution<float> component(-100.0F, 100.0F);
    for (int i = 0; i < 10000; ++i) {
        light.direction = {component(random), component(random), component(random)};
        if (lengthOf(light.direction) < 0.01) {
            continue;
        }
        REQUIRE_FALSE(scene.replaceComponent(entity, light).has_value());
        const Vec3 stored = scene.component<DirectionalLightComponent>(entity).value().direction;
        CHECK(std::abs(lengthOf(stored) - 1.0) <= 1.0e-6);
        DirectionalLightComponent again = light;
        again.direction = stored;
        REQUIRE_FALSE(scene.replaceComponent(entity, again).has_value());
        CHECK(
            sameBits(scene.component<DirectionalLightComponent>(entity).value().direction, stored));
    }
}

TEST_CASE("100 000 entities with components are created, queried and destroyed",
          "[scene][components]")
{
    constexpr int kCount = 100000;
    Scene scene = makeScene();
    std::vector<EntityId> entities;
    entities.reserve(kCount);
    std::vector<EntityId> named;
    std::vector<EntityId> cameras;
    for (int i = 0; i < kCount; ++i) {
        const EntityId entity = makeEntity(scene);
        entities.push_back(entity);
        TransformComponent transform;
        transform.position = {static_cast<float>(i), 0.0F, 0.0F};
        REQUIRE_FALSE(scene.addComponent(entity, transform).has_value());
        if (i % 2 == 0) {
            REQUIRE_FALSE(scene.addComponent(entity, NameComponent{std::to_string(i)}).has_value());
            named.push_back(entity);
        }
        if (i % 1000 == 0) {
            REQUIRE_FALSE(scene.addComponent(entity, CameraComponent{}).has_value());
            cameras.push_back(entity);
        }
    }
    CHECK(scene.entitiesWith<TransformComponent>() == entities);
    CHECK(scene.entitiesWith<NameComponent>() == named);
    CHECK(scene.entitiesWith<CameraComponent, NameComponent>() == cameras);
    CHECK(scene.component<TransformComponent>(entities[54321]).value().position.x == 54321.0F);
    CHECK(scene.component<NameComponent>(entities[4242]).value().value == "4242");

    for (std::size_t i = 0; i < entities.size(); i += 2) {
        REQUIRE_FALSE(scene.destroyEntity(entities[i]).has_value());
    }
    CHECK(scene.entityCount() == static_cast<std::size_t>(kCount / 2));
    CHECK(scene.entitiesWith<NameComponent>().empty());
    CHECK(scene.entitiesWith<CameraComponent>().empty());
    const std::vector<EntityId> transforms = scene.entitiesWith<TransformComponent>();
    REQUIRE(transforms.size() == static_cast<std::size_t>(kCount / 2));
    CHECK(transforms.front() == entities[1]);
    CHECK(transforms.back() == entities.back());

    for (std::size_t i = 1; i < entities.size(); i += 2) {
        REQUIRE_FALSE(scene.destroyEntity(entities[i]).has_value());
    }
    CHECK(scene.entityCount() == 0);
    CHECK(scene.entitiesWith<TransformComponent>().empty());
}
