// What a scene asks the renderer to draw (M5.6, engine/world/SceneDraws.hpp):
// which entities, with which model matrices, in which light. Headless.

#include "engine/core/Uuid.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/world/SceneDraws.hpp"

#include <glm/gtc/quaternion.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <utility>

using engine::assets::AssetId;
using engine::math::Mat4;
using engine::math::Vec3;
using engine::math::Vec4;
using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::Scene;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;
using engine::world::MeshDraw;
using engine::world::meshDraws;
using engine::world::modelMatrix;
using engine::world::sceneLight;

namespace {

Scene makeScene()
{
    auto scene = Scene::create("Draws", engine::core::UuidGenerator(7U));
    REQUIRE(scene.hasValue());
    return std::move(scene).value();
}

EntityId makeEntity(Scene& scene)
{
    auto entity = scene.createEntity();
    REQUIRE(entity.hasValue());
    return entity.value();
}

template <typename T>
void add(Scene& scene, EntityId entity, const T& component)
{
    REQUIRE_FALSE(scene.addComponent(entity, component).has_value());
}

AssetId assetId(std::uint64_t n)
{
    return AssetId{engine::core::Uuid{0xA55E7ULL, n}};
}

Vec3 apply(const Mat4& m, const Vec3& point)
{
    const Vec4 p = m * Vec4{point, 1.0F};
    return Vec3{p} / p.w;
}

bool near(const Vec3& a, const Vec3& b, float tolerance = 1.0e-5F)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
           std::abs(a.z - b.z) <= tolerance;
}

bool equal(const Mat4& a, const Mat4& b, float tolerance = 1.0e-6F)
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

TransformComponent transform(Vec3 position, float yawDegrees, Vec3 scale)
{
    TransformComponent t;
    t.position = position;
    t.rotation = glm::angleAxis(glm::radians(yawDegrees), Vec3{0.0F, 1.0F, 0.0F});
    t.scale = scale;
    return t;
}

} // namespace

TEST_CASE("A transform's model matrix scales, then rotates, then translates", "[world][scene]")
{
    // The defaults: the identity.
    CHECK(equal(modelMatrix(TransformComponent{}), Mat4{1.0F}));

    // Each part alone.
    CHECK(near(apply(modelMatrix(transform({1.0F, 2.0F, 3.0F}, 0.0F, Vec3{1.0F})), Vec3{0.0F}),
               {1.0F, 2.0F, 3.0F}));
    CHECK(near(apply(modelMatrix(transform(Vec3{0.0F}, 90.0F, Vec3{1.0F})), {1.0F, 0.0F, 0.0F}),
               {0.0F, 0.0F, -1.0F}));
    CHECK(near(
        apply(modelMatrix(transform(Vec3{0.0F}, 0.0F, {2.0F, 3.0F, 4.0F})), {1.0F, 1.0F, 1.0F}),
        {2.0F, 3.0F, 4.0F}));

    // Together, golden: the point (1, 1, 0) scaled by (2, 3, 1) is (2, 3, 0),
    // turned 90 degrees about +Y is (0, 3, -2), moved by (10, 0, 5) is
    // (10, 3, 3). The other order (rotate, then scale) would give (0, 3, -1).
    const Mat4 model = modelMatrix(transform({10.0F, 0.0F, 5.0F}, 90.0F, {2.0F, 3.0F, 1.0F}));
    CHECK(near(apply(model, {1.0F, 1.0F, 0.0F}), {10.0F, 3.0F, 3.0F}));
    // The origin goes to the position, whatever the rotation and scale.
    CHECK(near(apply(model, Vec3{0.0F}), {10.0F, 0.0F, 5.0F}));
}

TEST_CASE("The entities with a static mesh are drawn, in creation order, at their transforms",
          "[world][scene]")
{
    Scene scene = makeScene();
    CHECK(meshDraws(scene).empty());

    const EntityId named = makeEntity(scene);
    add(scene, named, NameComponent{"Only a name"});
    const EntityId first = makeEntity(scene);
    const TransformComponent firstAt = transform({1.0F, 2.0F, 3.0F}, 30.0F, Vec3{2.0F});
    add(scene, first, firstAt);
    add(scene, first, StaticMeshComponent{assetId(1), {}});
    // A transform and a camera, but no mesh: not drawn.
    const EntityId camera = makeEntity(scene);
    add(scene, camera, TransformComponent{});
    add(scene, camera, CameraComponent{});
    // A mesh without a transform: at the transform's defaults.
    const EntityId bare = makeEntity(scene);
    add(scene, bare, StaticMeshComponent{assetId(2), assetId(9)});
    // A child: its own transform only, its parent's does not move it.
    const EntityId child = makeEntity(scene);
    const TransformComponent childAt = transform({-4.0F, 0.0F, 0.0F}, 0.0F, Vec3{1.0F});
    add(scene, child, childAt);
    add(scene, child, StaticMeshComponent{assetId(1), {}});
    add(scene, child, ParentComponent{first});

    const auto draws = meshDraws(scene);
    REQUIRE(draws.size() == 3);
    CHECK(draws[0].entity == first);
    CHECK(draws[0].mesh == assetId(1));
    CHECK(equal(draws[0].model, modelMatrix(firstAt)));
    CHECK(draws[1].entity == bare);
    CHECK(draws[1].mesh == assetId(2));
    CHECK(equal(draws[1].model, Mat4{1.0F}));
    CHECK(draws[2].entity == child);
    CHECK(draws[2].mesh == assetId(1));
    CHECK(equal(draws[2].model, modelMatrix(childAt)));

    // Changes show in the next list: a moved entity, a removed mesh, a
    // destroyed entity and its return at its place.
    const TransformComponent moved = transform({0.0F, 50.0F, 0.0F}, 180.0F, Vec3{0.5F});
    REQUIRE_FALSE(scene.replaceComponent(child, moved).has_value());
    REQUIRE_FALSE(scene.removeComponent<StaticMeshComponent>(bare).has_value());
    const auto place = scene.placeOf(first);
    REQUIRE(place.hasValue());
    REQUIRE_FALSE(scene.destroyEntity(first).has_value());
    auto changed = meshDraws(scene);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].entity == child);
    CHECK(equal(changed[0].model, modelMatrix(moved)));

    REQUIRE(scene.restoreEntity(first, place.value()).hasValue());
    add(scene, first, firstAt);
    add(scene, first, StaticMeshComponent{assetId(1), {}});
    changed = meshDraws(scene);
    REQUIRE(changed.size() == 2);
    CHECK(changed[0].entity == first);
    CHECK(changed[1].entity == child);
}

TEST_CASE("The scene's first directional light lights its entities, else the world's sun",
          "[world][scene]")
{
    const engine::render::LightDesc sun{
        .direction = {0.0F, -1.0F, 0.0F}, .color = Vec3{0.5F}, .ambient = {0.2F, 0.3F, 0.4F}};
    Scene scene = makeScene();
    const EntityId mesh = makeEntity(scene);
    add(scene, mesh, StaticMeshComponent{assetId(1), {}});

    // No light in the scene: the world's sun, unchanged.
    engine::render::LightDesc light = sceneLight(scene, sun);
    CHECK(light.direction == sun.direction);
    CHECK(light.color == sun.color);
    CHECK(light.ambient == sun.ambient);

    // The first light in creation order: its direction (stored normalized),
    // color times intensity, and the sun's ambient, which it has none of.
    const EntityId evening = makeEntity(scene);
    add(scene, evening,
        DirectionalLightComponent{
            .direction = {0.0F, -3.0F, 4.0F}, .color = {1.0F, 0.5F, 0.25F}, .intensity = 2.0F});
    const EntityId second = makeEntity(scene);
    add(scene, second,
        DirectionalLightComponent{
            .direction = {1.0F, 0.0F, 0.0F}, .color = Vec3{1.0F}, .intensity = 1.0F});
    light = sceneLight(scene, sun);
    CHECK(light.direction.x == Catch::Approx(0.0F).margin(1.0e-6));
    CHECK(light.direction.y == Catch::Approx(-0.6F));
    CHECK(light.direction.z == Catch::Approx(0.8F));
    CHECK(light.color == Vec3{2.0F, 1.0F, 0.5F});
    CHECK(light.ambient == sun.ambient);
    CHECK_FALSE(engine::render::validate(light).has_value());

    // Without the first, the second; without both, the sun again.
    REQUIRE_FALSE(scene.destroyEntity(evening).has_value());
    CHECK(sceneLight(scene, sun).direction == Vec3{1.0F, 0.0F, 0.0F});
    REQUIRE_FALSE(scene.removeComponent<DirectionalLightComponent>(second).has_value());
    CHECK(sceneLight(scene, sun).color == sun.color);

    // An intensity of zero: no direct light, only the ambient.
    const EntityId dark = makeEntity(scene);
    add(scene, dark, DirectionalLightComponent{.intensity = 0.0F});
    CHECK(sceneLight(scene, sun).color == Vec3{0.0F});
}
