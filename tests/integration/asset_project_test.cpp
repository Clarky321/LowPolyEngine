// A project folder on disk (M4.3): its asset manifest is saved, the folder is
// moved, and the manifest loaded from the new place resolves the same asset
// IDs to the moved files, which the CPU loaders read.

#include "engine/assets/AssetManifest.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/core/Image.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/geometry/ObjImport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>

using engine::assets::AssetAvailability;
using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;

namespace {

// Removes the folders a test creates, at scope exit.
class TempFolders {
public:
    TempFolders() : base_(std::filesystem::temp_directory_path() / "lpe_integration_asset_project")
    {
        std::error_code ignored;
        std::filesystem::remove_all(base_, ignored);
        std::filesystem::create_directories(base_);
    }
    ~TempFolders()
    {
        std::error_code ignored;
        std::filesystem::remove_all(base_, ignored);
    }
    TempFolders(const TempFolders&) = delete;
    TempFolders& operator=(const TempFolders&) = delete;

    [[nodiscard]] const std::filesystem::path& base() const { return base_; }

private:
    std::filesystem::path base_;
};

} // namespace

TEST_CASE("A moved project resolves the same asset IDs to the moved files", "[integration][assets]")
{
    const TempFolders folders;
    const std::filesystem::path source{LPE_ASSET_DIRECTORY};
    const std::filesystem::path before = folders.base() / "before" / "MyProject";
    std::filesystem::create_directories(before / "models");
    std::filesystem::create_directories(before / "textures");
    std::filesystem::copy_file(source / "models" / "lighthouse.obj",
                               before / "models" / "lighthouse.obj");
    std::filesystem::copy_file(source / "textures" / "lighthouse.png",
                               before / "textures" / "lighthouse.png");

    engine::core::UuidGenerator generator(1);
    const AssetId mesh{generator.next()};
    const AssetId texture{generator.next()};
    const AssetId material{generator.next()};
    {
        AssetRegistry registry(before);
        REQUIRE_FALSE(registry
                          .registerAsset(mesh, AssetType::Mesh,
                                         AssetPath::parse("models/lighthouse.obj").value())
                          .has_value());
        REQUIRE_FALSE(registry
                          .registerAsset(texture, AssetType::Texture,
                                         AssetPath::parse("textures/lighthouse.png").value())
                          .has_value());
        REQUIRE_FALSE(registry
                          .registerAsset(material, AssetType::Material,
                                         AssetPath::parse("materials/lighthouse.mat").value())
                          .has_value());
        REQUIRE_FALSE(engine::assets::saveManifest(before / "assets.json", registry).has_value());
    }

    // The manifest names no machine-specific path.
    const auto text = engine::core::readTextFile(before / "assets.json");
    REQUIRE(text);
    CHECK(text.value().find("MyProject") == std::string::npos);
    CHECK(text.value().find(engine::core::toUtf8(folders.base())) == std::string::npos);

    const std::filesystem::path after = folders.base() / "elsewhere" / "Renamed";
    std::filesystem::create_directories(after.parent_path());
    std::filesystem::rename(before, after);
    REQUIRE_FALSE(std::filesystem::exists(before));

    const auto loaded = engine::assets::loadManifest(after / "assets.json", after);
    REQUIRE(loaded);
    CHECK(loaded.value().warnings.empty());
    const AssetRegistry& registry = loaded.value().registry;
    CHECK(registry.size() == 3);
    CHECK(registry.find(mesh).value().availability == AssetAvailability::Present);
    CHECK(registry.find(texture).value().availability == AssetAvailability::Present);
    CHECK(registry.find(material).value().availability == AssetAvailability::Missing);

    const auto meshFile = registry.resolve(mesh, AssetType::Mesh);
    REQUIRE(meshFile);
    CHECK(meshFile.value() == after / "models" / "lighthouse.obj");
    const auto obj = engine::geometry::readObj(meshFile.value());
    REQUIRE(obj);
    CHECK_FALSE(obj.value().mesh.positions.empty());

    const auto textureFile = registry.resolve(texture, AssetType::Texture);
    REQUIRE(textureFile);
    const auto image = engine::core::readPng(textureFile.value());
    REQUIRE(image);
    CHECK(image.value().width == 32);

    CHECK_FALSE(registry.resolve(material, AssetType::Material));
}
