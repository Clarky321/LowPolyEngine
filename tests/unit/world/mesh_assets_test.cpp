// Mesh assets read for drawing (M5.6, engine/world/MeshAssets.hpp): each read
// once through the registry and the OBJ loader, failures kept and reported
// once, the cache across a scene's changes and for another project. Headless.

#include "../assets/TempProject.hpp"

#include "engine/assets/AssetPath.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/AssetType.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/world/MeshAssets.hpp"
#include "engine/world/SceneDraws.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::world::MeshAsset;
using engine::world::MeshAssets;
using lpe_test::assetId;
using lpe_test::TempProject;

namespace {

// One triangle, and a square of two.
constexpr std::string_view kTriangle = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
constexpr std::string_view kSquare = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3\nf 1 3 4\n";
// The face names a position that does not exist.
constexpr std::string_view kBroken = "v 0 0 0\nv 1 0 0\nf 1 2 3\n";

void registerAsset(AssetRegistry& registry, AssetId id, AssetType type, std::string_view path)
{
    REQUIRE_FALSE(registry.registerAsset(id, type, AssetPath::parse(path).value()).has_value());
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

std::size_t verticesOf(const MeshAssets& meshes, AssetId id)
{
    const MeshAsset* asset = meshes.find(id);
    REQUIRE(asset != nullptr);
    REQUIRE(asset->mesh.has_value());
    CHECK(asset->problem.empty());
    return asset->mesh->positions.size();
}

} // namespace

TEST_CASE("Mesh assets are read once, and every failure is reported once", "[world][assets]")
{
    const TempProject project("lpe_unit_mesh_assets");
    project.write("models/triangle.obj", kTriangle);
    project.write("models/broken.obj", kBroken);
    project.write("textures/crate.png", "not read");
    AssetRegistry registry(project.root());
    registerAsset(registry, assetId(1), AssetType::Mesh, "models/triangle.obj");
    registerAsset(registry, assetId(2), AssetType::Mesh, "models/broken.obj");
    registerAsset(registry, assetId(3), AssetType::Mesh, "models/missing.obj");
    registerAsset(registry, assetId(4), AssetType::Texture, "textures/crate.png");
    const AssetId unregistered = assetId(5);

    MeshAssets meshes;
    CHECK(meshes.size() == 0);
    CHECK(meshes.find(assetId(1)) == nullptr);

    // The triangle twice in the list: read once. Every other asset fails,
    // with a message that names it and the reason.
    const std::vector<std::string> problems = meshes.load(
        {assetId(1), assetId(2), assetId(1), assetId(3), assetId(4), unregistered}, registry);
    REQUIRE(problems.size() == 4);
    CHECK(contains(problems[0], assetId(2).toString()));
    CHECK(contains(problems[0], "(mesh 'models/broken.obj')"));
    CHECK(contains(problems[0], "line 3"));
    CHECK(contains(problems[1], assetId(3).toString()));
    CHECK(contains(problems[1], "models/missing.obj"));
    CHECK(contains(problems[2], "'textures/crate.png' is a texture, expected a mesh"));
    CHECK(problems[3] == "asset " + unregistered.toString() + ": not registered");
    CHECK(meshes.size() == 5);
    CHECK(verticesOf(meshes, assetId(1)) == 3);
    for (const AssetId failed : {assetId(2), assetId(3), assetId(4), unregistered}) {
        const MeshAsset* asset = meshes.find(failed);
        REQUIRE(asset != nullptr);
        CHECK_FALSE(asset->mesh.has_value());
        CHECK_FALSE(asset->problem.empty());
    }

    // Asked again: no file is read and nothing is reported again. The
    // triangle's file is replaced and the missing file appears, but the cache
    // keeps what it read.
    project.write("models/triangle.obj", kSquare);
    project.write("models/missing.obj", kTriangle);
    CHECK(meshes.load({assetId(4), assetId(1), assetId(3), assetId(2), unregistered}, registry)
              .empty());
    CHECK(meshes.size() == 5);
    CHECK(verticesOf(meshes, assetId(1)) == 3);
    CHECK_FALSE(meshes.find(assetId(3))->mesh.has_value());

    // Cleared, everything is read again, as it is now.
    meshes.clear();
    CHECK(meshes.size() == 0);
    const auto again = meshes.load({assetId(1), assetId(3)}, registry);
    CHECK(again.empty());
    CHECK(verticesOf(meshes, assetId(1)) == 6);
    CHECK(verticesOf(meshes, assetId(3)) == 3);
}

TEST_CASE("The mesh cache follows a scene's changes and starts again for another project",
          "[world][assets]")
{
    const TempProject first("lpe_unit_mesh_assets_first");
    first.write("models/tower.obj", kTriangle);
    first.write("models/wall.obj", kSquare);
    AssetRegistry registry(first.root());
    registerAsset(registry, assetId(1), AssetType::Mesh, "models/tower.obj");
    registerAsset(registry, assetId(2), AssetType::Mesh, "models/wall.obj");

    auto created = engine::scene::Scene::create("Harbour", engine::core::UuidGenerator(3U));
    REQUIRE(created.hasValue());
    engine::scene::Scene scene = std::move(created).value();
    const auto meshesOf = [&scene] {
        std::vector<AssetId> ids;
        for (const auto& draw : engine::world::meshDraws(scene)) {
            ids.push_back(draw.mesh);
        }
        return ids;
    };
    const auto addTower = [&scene](AssetId mesh) {
        auto entity = scene.createEntity();
        REQUIRE(entity.hasValue());
        REQUIRE_FALSE(
            scene.addComponent(entity.value(), engine::scene::StaticMeshComponent{mesh, {}})
                .has_value());
        return entity.value();
    };

    MeshAssets meshes;
    const auto tower = addTower(assetId(1));
    CHECK(meshes.load(meshesOf(), registry).empty());
    CHECK(meshes.size() == 1);

    // The tower's file goes: entities added, removed and added again with
    // the same mesh read nothing, so nothing fails.
    first.remove("models/tower.obj");
    const auto second = addTower(assetId(1));
    CHECK(meshes.load(meshesOf(), registry).empty());
    REQUIRE_FALSE(scene.destroyEntity(tower).has_value());
    REQUIRE_FALSE(scene.destroyEntity(second).has_value());
    CHECK(meshes.load(meshesOf(), registry).empty());
    (void)addTower(assetId(1));
    CHECK(meshes.load(meshesOf(), registry).empty());
    CHECK(verticesOf(meshes, assetId(1)) == 3);
    // A second mesh: one more read, the first stays.
    (void)addTower(assetId(2));
    CHECK(meshes.load(meshesOf(), registry).empty());
    CHECK(meshes.size() == 2);
    CHECK(verticesOf(meshes, assetId(2)) == 6);

    // Another project uses the same asset ID for another file. Its meshes
    // are read from it once the cache is cleared for it.
    const TempProject other("lpe_unit_mesh_assets_other");
    other.write("models/tower.obj", kSquare);
    AssetRegistry otherRegistry(other.root());
    registerAsset(otherRegistry, assetId(1), AssetType::Mesh, "models/tower.obj");
    meshes.clear();
    CHECK(meshes.load({assetId(1)}, otherRegistry).empty());
    CHECK(meshes.size() == 1);
    CHECK(verticesOf(meshes, assetId(1)) == 6);
    // The first project's other mesh is unknown there.
    const auto unknown = meshes.load({assetId(2)}, otherRegistry);
    REQUIRE(unknown.size() == 1);
    CHECK(contains(unknown[0], "not registered"));
}
