#include "../assets/TempProject.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/AssetReferences.hpp"
#include "engine/scene/Scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::core::Uuid;
using engine::core::UuidGenerator;
using engine::scene::EntityId;
using engine::scene::Scene;
using engine::scene::SceneId;
using engine::scene::StaticMeshComponent;
using lpe_test::assetId;

namespace {

Scene makeScene()
{
    auto created = Scene::create(SceneId{Uuid{0x0123456789ABCDEFULL, 0x8000000000000001ULL}},
                                 "Main", UuidGenerator(3));
    REQUIRE(created);
    return std::move(created).value();
}

EntityId addMesh(Scene& scene, AssetId mesh, AssetId material)
{
    const EntityId entity = scene.createEntity().value();
    REQUIRE_FALSE(scene.addComponent(entity, StaticMeshComponent{mesh, material}).has_value());
    return entity;
}

AssetRegistry makeRegistry()
{
    AssetRegistry registry(std::filesystem::path("unused"));
    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Mesh,
                                         AssetPath::parse("models/tree.obj").value()));
    REQUIRE_FALSE(registry.registerAsset(assetId(2), AssetType::Material,
                                         AssetPath::parse("materials/bark.mat").value()));
    REQUIRE_FALSE(registry.registerAsset(assetId(3), AssetType::Texture,
                                         AssetPath::parse("textures/bark.png").value()));
    return registry;
}

} // namespace

TEST_CASE("Valid asset references are no problems", "[scene][assets]")
{
    Scene scene = makeScene();
    (void)addMesh(scene, assetId(1), assetId(2));
    // A null material means the default material.
    (void)addMesh(scene, assetId(1), AssetId{});
    CHECK(engine::scene::findAssetReferenceProblems(scene, makeRegistry()).empty());
    CHECK(engine::scene::findAssetReferenceProblems(makeScene(), makeRegistry()).empty());
}

TEST_CASE("Unknown and wrongly typed asset references are reported in entity order",
          "[scene][assets]")
{
    Scene scene = makeScene();
    const EntityId unknownMesh = addMesh(scene, assetId(9), AssetId{});
    const EntityId fine = addMesh(scene, assetId(1), assetId(2));
    const EntityId wrongTypes = addMesh(scene, assetId(2), assetId(3));
    (void)fine;

    const auto problems = engine::scene::findAssetReferenceProblems(scene, makeRegistry());
    REQUIRE(problems.size() == 3);

    CHECK(problems[0].entity == unknownMesh);
    CHECK(problems[0].component == "static mesh");
    CHECK(problems[0].field == "mesh");
    CHECK(problems[0].asset == assetId(9));
    CHECK(problems[0].message == "scene 'Main': entity " + unknownMesh.toString() +
                                     ": static mesh component: mesh: asset " +
                                     assetId(9).toString() + ": not registered");

    CHECK(problems[1].entity == wrongTypes);
    CHECK(problems[1].field == "mesh");
    CHECK(problems[1].message == "scene 'Main': entity " + wrongTypes.toString() +
                                     ": static mesh component: mesh: asset " +
                                     assetId(2).toString() +
                                     ": 'materials/bark.mat' is a material, expected a mesh");
    CHECK(problems[2].entity == wrongTypes);
    CHECK(problems[2].field == "material");
    CHECK(problems[2].asset == assetId(3));
    CHECK(problems[2].message.ends_with("'textures/bark.png' is a texture, expected a material"));
}
