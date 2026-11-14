#include "TempProject.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Image.hpp"
#include "engine/geometry/ObjImport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using engine::assets::AssetAvailability;
using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using lpe_test::assetId;
using lpe_test::TempProject;

namespace {

AssetPath pathOf(std::string_view text)
{
    auto path = AssetPath::parse(text);
    REQUIRE(path);
    return path.value();
}

std::string messageOf(const std::optional<engine::core::Error>& error)
{
    REQUIRE(error.has_value());
    return error->message;
}

std::string idText(std::uint64_t number)
{
    return assetId(number).toString();
}

} // namespace

TEST_CASE("Assets are registered and found by ID and by path", "[assets][registry]")
{
    const TempProject project("lpe_assets_register");
    project.write("models/tree.obj");
    AssetRegistry registry(project.root());
    CHECK(registry.projectRoot() == project.root());
    CHECK(registry.size() == 0);

    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("models/tree.obj")));
    REQUIRE_FALSE(registry.registerAsset(assetId(2), AssetType::Texture, pathOf("bark.png")));
    REQUIRE_FALSE(
        registry.registerAsset(assetId(3), AssetType::Material, pathOf("materials/bark.mat")));
    CHECK(registry.size() == 3);
    CHECK(registry.contains(assetId(1)));
    CHECK_FALSE(registry.contains(assetId(4)));

    const auto tree = registry.find(assetId(1));
    REQUIRE(tree);
    CHECK(tree.value().id == assetId(1));
    CHECK(tree.value().type == AssetType::Mesh);
    CHECK(tree.value().path == pathOf("models/tree.obj"));
    CHECK(tree.value().sourceName() == "tree.obj");
    CHECK(tree.value().availability == AssetAvailability::Present);
    // Registered although the file is not there yet.
    CHECK(registry.find(assetId(2)).value().availability == AssetAvailability::Missing);

    CHECK(registry.findByPath(pathOf("models/tree.obj")) == assetId(1));
    CHECK(registry.findByPath(pathOf("./models//tree.obj")) == assetId(1));
    CHECK_FALSE(registry.findByPath(pathOf("Models/tree.obj")).has_value());
    CHECK_FALSE(registry.findByPath(pathOf("models/oak.obj")).has_value());

    // In path order, compared in folded case.
    const auto all = registry.assets();
    REQUIRE(all.size() == 3);
    CHECK(all[0].id == assetId(2));
    CHECK(all[1].id == assetId(3));
    CHECK(all[2].id == assetId(1));

    const auto unknown = registry.find(assetId(9));
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().message == "asset " + idText(9) + ": not registered");
    CHECK(registry.find(AssetId{}).error().message ==
          "asset registry: the null asset ID refers to no asset");
}

TEST_CASE("The same ID or path is registered once", "[assets][registry]")
{
    const TempProject project("lpe_assets_duplicates");
    AssetRegistry registry(project.root());
    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("models/tree.obj")));

    CHECK(messageOf(registry.registerAsset(assetId(1), AssetType::Texture, pathOf("a.png"))) ==
          "asset " + idText(1) + ": already registered as mesh 'models/tree.obj'");
    CHECK(
        messageOf(registry.registerAsset(assetId(2), AssetType::Mesh, pathOf("models/tree.obj"))) ==
        "asset " + idText(2) + ": path 'models/tree.obj' is already used by asset " + idText(1));
    CHECK(messageOf(registry.registerAsset(AssetId{}, AssetType::Mesh, pathOf("b.obj"))) ==
          "asset registry: the null asset ID is not an identity");
    CHECK(registry.size() == 1);
}

TEST_CASE("Paths that differ only in letter case conflict", "[assets][registry]")
{
    const TempProject project("lpe_assets_case");
    AssetRegistry registry(project.root());
    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("models/tree.obj")));

    CHECK(
        messageOf(registry.registerAsset(assetId(2), AssetType::Mesh, pathOf("models/Tree.OBJ"))) ==
        "asset " + idText(2) +
            ": path 'models/Tree.OBJ' differs only in letter case from 'models/tree.obj' of "
            "asset " +
            idText(1) + ": one file on Windows, two on Linux");
    // A folder spelled differently would be one folder on Windows, two on Linux.
    CHECK(
        messageOf(registry.registerAsset(assetId(3), AssetType::Mesh, pathOf("Models/oak.obj"))) ==
        "asset " + idText(3) +
            ": folder 'Models' of 'Models/oak.obj' differs only in letter case from the "
            "registered folder 'models'");
    // A file and a folder of the same name cannot both exist.
    CHECK(messageOf(registry.registerAsset(assetId(4), AssetType::Material, pathOf("Models"))) ==
          "asset " + idText(4) + ": path 'Models' is a folder of registered assets");
    CHECK(messageOf(registry.registerAsset(assetId(5), AssetType::Texture,
                                           pathOf("models/tree.obj/bark.png"))) ==
          "asset " + idText(5) +
              ": folder 'models/tree.obj' of 'models/tree.obj/bark.png' is the "
              "file of asset " +
              idText(1));
    CHECK(registry.size() == 1);

    // Same folder spelling: fine. After the last asset in a folder is gone,
    // another spelling may be used.
    REQUIRE_FALSE(registry.registerAsset(assetId(6), AssetType::Mesh, pathOf("models/oak.obj")));
    REQUIRE_FALSE(registry.unregisterAsset(assetId(1)));
    CHECK(
        registry.registerAsset(assetId(3), AssetType::Mesh, pathOf("Models/pine.obj")).has_value());
    REQUIRE_FALSE(registry.unregisterAsset(assetId(6)));
    REQUIRE_FALSE(registry.registerAsset(assetId(3), AssetType::Mesh, pathOf("Models/pine.obj")));
    REQUIRE_FALSE(registry.registerAsset(assetId(7), AssetType::Mesh, pathOf("models.obj")));
}

TEST_CASE("Asset types allow only their extensions", "[assets][registry]")
{
    const TempProject project("lpe_assets_extensions");
    AssetRegistry registry(project.root());
    CHECK(messageOf(registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("tree.fbx"))) ==
          "asset " + idText(1) + ": mesh 'tree.fbx' must have the extension .obj");
    CHECK(messageOf(registry.registerAsset(assetId(1), AssetType::Texture, pathOf("bark.jpg"))) ==
          "asset " + idText(1) + ": texture 'bark.jpg' must have the extension .png");
    CHECK(messageOf(registry.registerAsset(assetId(1), AssetType::Script, pathOf("door"))) ==
          "asset " + idText(1) + ": script 'door' must have the extension .lua");
    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("TREE.OBJ")));
    REQUIRE_FALSE(registry.registerAsset(assetId(2), AssetType::Texture, pathOf("bark.Png")));
    REQUIRE_FALSE(registry.registerAsset(assetId(3), AssetType::Script, pathOf("door.lua")));
    REQUIRE_FALSE(registry.registerAsset(assetId(4), AssetType::Material, pathOf("bark.anything")));
    REQUIRE_FALSE(registry.registerAsset(assetId(5), AssetType::Prefab, pathOf("prefabs/house")));
}

TEST_CASE("Unregistering removes an asset", "[assets][registry]")
{
    const TempProject project("lpe_assets_unregister");
    AssetRegistry registry(project.root());
    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("tree.obj")));
    REQUIRE_FALSE(registry.unregisterAsset(assetId(1)));
    CHECK_FALSE(registry.contains(assetId(1)));
    CHECK_FALSE(registry.findByPath(pathOf("tree.obj")).has_value());
    CHECK(messageOf(registry.unregisterAsset(assetId(1))) ==
          "asset " + idText(1) + ": not registered");
    CHECK(messageOf(registry.unregisterAsset(AssetId{})) ==
          "asset registry: the null asset ID refers to no asset");
    // The ID and the path are free again.
    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("tree.obj")));
}

TEST_CASE("Type checks name the asset and both types", "[assets][registry]")
{
    const TempProject project("lpe_assets_types");
    AssetRegistry registry(project.root());
    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Texture, pathOf("crate.png")));
    CHECK_FALSE(registry.checkType(assetId(1), AssetType::Texture).has_value());
    CHECK(messageOf(registry.checkType(assetId(1), AssetType::Mesh)) ==
          "asset " + idText(1) + ": 'crate.png' is a texture, expected a mesh");
    CHECK(messageOf(registry.checkType(assetId(2), AssetType::Mesh)) ==
          "asset " + idText(2) + ": not registered");
    CHECK(registry.resolve(assetId(1), AssetType::Mesh).error().message ==
          "asset " + idText(1) + ": 'crate.png' is a texture, expected a mesh");
}

TEST_CASE("Missing files are a state; resolving them fails", "[assets][registry]")
{
    const TempProject project("lpe_assets_missing");
    project.write("textures/crate.png");
    AssetRegistry registry(project.root());
    REQUIRE_FALSE(
        registry.registerAsset(assetId(1), AssetType::Texture, pathOf("textures/crate.png")));
    REQUIRE_FALSE(
        registry.registerAsset(assetId(2), AssetType::Texture, pathOf("textures/grass.png")));
    REQUIRE_FALSE(registry.registerAsset(assetId(3), AssetType::Texture, pathOf("rock.png")));

    const auto crate = registry.resolve(assetId(1), AssetType::Texture);
    REQUIRE(crate);
    CHECK(crate.value() == project.root() / "textures" / "crate.png");
    const auto grass = registry.resolve(assetId(2), AssetType::Texture);
    REQUIRE_FALSE(grass);
    CHECK(grass.error().message.starts_with("asset " + idText(2) +
                                            " (texture 'textures/grass.png'): file not found: '"));

    CHECK(registry.refreshAvailability() == std::vector<AssetId>{assetId(3), assetId(2)});
    project.write("rock.png");
    project.remove("textures/crate.png");
    CHECK(registry.refreshAvailability() == std::vector<AssetId>{assetId(1), assetId(2)});
    CHECK(registry.find(assetId(3)).value().availability == AssetAvailability::Present);
    CHECK(registry.find(assetId(1)).value().availability == AssetAvailability::Missing);

    // A folder where the file should be is not a file.
    std::filesystem::create_directories(project.root() / "textures" / "grass.png");
    CHECK(registry.resolve(assetId(2), AssetType::Texture)
              .error()
              .message.find("not a regular file") != std::string::npos);
}

TEST_CASE("A file with another letter case on disk is missing on every platform",
          "[assets][registry]")
{
    const TempProject project("lpe_assets_disk_case");
    project.write("Textures/Crate.png");
    AssetRegistry registry(project.root());
    REQUIRE_FALSE(
        registry.registerAsset(assetId(1), AssetType::Texture, pathOf("textures/crate.png")));
    CHECK(registry.find(assetId(1)).value().availability == AssetAvailability::Missing);
    CHECK(registry.resolve(assetId(1), AssetType::Texture).error().message ==
          "asset " + idText(1) +
              " (texture 'textures/crate.png'): file not found with this letter case; on disk it "
              "is 'Textures/Crate.png'");
}

TEST_CASE("A moved project root resolves the same assets", "[assets][registry]")
{
    const TempProject first("lpe_assets_root_a");
    const TempProject second("lpe_assets_root_b");
    first.write("models/tree.obj", "first");
    second.write("models/tree.obj", "second");
    for (const TempProject* project : {&first, &second}) {
        AssetRegistry registry(project->root());
        REQUIRE_FALSE(
            registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("models/tree.obj")));
        const auto file = registry.resolve(assetId(1), AssetType::Mesh);
        REQUIRE(file);
        CHECK(file.value() == project->root() / "models" / "tree.obj");
    }
}

TEST_CASE("Resolved meshes and textures load with the CPU loaders", "[assets][registry]")
{
    // The committed assets of island-preview as a project.
    AssetRegistry registry(std::filesystem::path(LPE_ASSET_DIRECTORY));
    REQUIRE_FALSE(
        registry.registerAsset(assetId(1), AssetType::Mesh, pathOf("models/lighthouse.obj")));
    REQUIRE_FALSE(
        registry.registerAsset(assetId(2), AssetType::Texture, pathOf("textures/lighthouse.png")));
    REQUIRE_FALSE(
        registry.registerAsset(assetId(3), AssetType::Texture, pathOf("textures/crate.png")));
    CHECK(registry.refreshAvailability().empty());

    const auto meshFile = registry.resolve(assetId(1), AssetType::Mesh);
    REQUIRE(meshFile);
    const auto mesh = engine::geometry::readObj(meshFile.value());
    REQUIRE(mesh);
    CHECK_FALSE(mesh.value().mesh.indices.empty());

    for (const AssetId texture : {assetId(2), assetId(3)}) {
        const auto textureFile = registry.resolve(texture, AssetType::Texture);
        REQUIRE(textureFile);
        const auto image = engine::core::readPng(textureFile.value());
        REQUIRE(image);
        CHECK(image.value().width > 0);
    }
    const auto crate =
        engine::core::readPng(registry.resolve(assetId(3), AssetType::Texture).value());
    CHECK(crate.value().width == 16);
    CHECK(crate.value().height == 16);
}
