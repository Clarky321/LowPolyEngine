// Scene documents on disk (M4.4): a scene that refers to the assets of a
// project is saved, the runtime scene destroyed, and the document loaded with
// the project's registry; failed saves and broken files change nothing.

#include "engine/assets/AssetManifest.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/scene/SceneDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::core::UuidGenerator;
using engine::scene::CameraComponent;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::Scene;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;

namespace {

constexpr auto kAllWrite = std::filesystem::perms::owner_write |
                           std::filesystem::perms::group_write |
                           std::filesystem::perms::others_write;

// A project folder removed at scope exit, write permissions restored first.
class TempProject {
public:
    explicit TempProject(const char* name) : root_(std::filesystem::temp_directory_path() / name)
    {
        cleanUp();
        std::filesystem::create_directories(root_ / "models");
        std::filesystem::create_directories(root_ / "scenes");
    }
    ~TempProject() { cleanUp(); }
    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    void cleanUp() const
    {
        std::error_code ignored;
        if (std::filesystem::exists(root_, ignored)) {
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(root_, ignored)) {
                std::filesystem::permissions(entry.path(), std::filesystem::perms::owner_write,
                                             std::filesystem::perm_options::add, ignored);
            }
        }
        std::filesystem::remove_all(root_, ignored);
    }

    std::filesystem::path root_;
};

std::string read(const std::filesystem::path& file)
{
    auto text = engine::core::readTextFile(file);
    REQUIRE(text);
    return text.value();
}

void write(const std::filesystem::path& file, const std::string& text)
{
    REQUIRE_FALSE(engine::core::writeBinaryFile(
                      file, std::span<const std::uint8_t>(
                                reinterpret_cast<const std::uint8_t*>(text.data()), text.size()))
                      .has_value());
}

} // namespace

TEST_CASE("A scene with asset references survives save, destruction and load",
          "[integration][scene]")
{
    const TempProject project("lpe_integration_scene_document");
    std::filesystem::copy_file(std::filesystem::path(LPE_ASSET_DIRECTORY) / "models" /
                                   "lighthouse.obj",
                               project.root() / "models" / "lighthouse.obj");
    UuidGenerator generator(1);
    const AssetId mesh{generator.next()};
    const AssetId material{generator.next()};
    {
        AssetRegistry registry(project.root());
        REQUIRE_FALSE(registry
                          .registerAsset(mesh, AssetType::Mesh,
                                         AssetPath::parse("models/lighthouse.obj").value())
                          .has_value());
        REQUIRE_FALSE(registry
                          .registerAsset(material, AssetType::Material,
                                         AssetPath::parse("materials/stone.mat").value())
                          .has_value());
        REQUIRE_FALSE(
            engine::assets::saveManifest(project.root() / "assets.json", registry).has_value());
    }

    const std::filesystem::path file = project.root() / "scenes" / "island.scene.json";
    std::string saved;
    std::vector<EntityId> ids;
    {
        auto created = Scene::create("Island", generator);
        REQUIRE(created);
        Scene scene = std::move(created).value();
        const EntityId lighthouse = scene.createEntity().value();
        REQUIRE_FALSE(scene.addComponent(lighthouse, NameComponent{"Lighthouse"}).has_value());
        TransformComponent transform;
        transform.position = {12.25F, 40.0F, -7.5F};
        REQUIRE_FALSE(scene.addComponent(lighthouse, transform).has_value());
        REQUIRE_FALSE(
            scene.addComponent(lighthouse, StaticMeshComponent{mesh, material}).has_value());
        const EntityId eye = scene.createEntity().value();
        REQUIRE_FALSE(scene.addComponent(eye, CameraComponent{}).has_value());
        ids = scene.entityIds();
        saved = engine::scene::sceneToJson(scene);
        REQUIRE_FALSE(engine::scene::saveScene(file, scene).has_value());
    } // The runtime scene is gone.
    CHECK(read(file) == saved);

    const auto manifest =
        engine::assets::loadManifest(project.root() / "assets.json", project.root());
    REQUIRE(manifest);
    const AssetRegistry& registry = manifest.value().registry;
    auto loaded = engine::scene::loadScene(file, UuidGenerator(2), &registry);
    REQUIRE(loaded);
    const Scene& scene = loaded.value().scene;
    CHECK(scene.name() == "Island");
    CHECK(scene.entityIds() == ids);
    CHECK(engine::scene::sceneToJson(scene) == saved);
    const StaticMeshComponent reference = scene.component<StaticMeshComponent>(ids[0]).value();
    CHECK(reference.mesh == mesh);
    CHECK(reference.material == material);
    // The mesh reference leads to a file the CPU loader can read.
    CHECK(registry.resolve(reference.mesh, AssetType::Mesh).hasValue());

    // Another project's registry does not know the assets: loading fails.
    const AssetRegistry other(project.root());
    const auto unknown = engine::scene::loadScene(file, UuidGenerator(3), &other);
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().message.find("not registered") != std::string::npos);
}

TEST_CASE("A failed save leaves the previous file unchanged", "[integration][scene]")
{
    const TempProject project("lpe_integration_scene_save");
    const std::filesystem::path folder = project.root() / "scenes";
    const std::filesystem::path file = folder / "main.scene.json";
    auto created = Scene::create("First", UuidGenerator(1));
    REQUIRE(created);
    const Scene first = std::move(created).value();
    REQUIRE_FALSE(engine::scene::saveScene(file, first).has_value());
    const std::string before = read(file);

    auto other = Scene::create("Second", UuidGenerator(2));
    REQUIRE(other);
    const Scene second = std::move(other).value();

    // A missing location.
    CHECK(engine::scene::saveScene(project.root() / "missing" / "main.scene.json", second)
              .has_value());
    CHECK_FALSE(std::filesystem::exists(project.root() / "missing"));

    // A read-only file in a read-only folder (Windows refuses to replace the
    // file, Linux to create the temporary file).
    std::filesystem::permissions(file, kAllWrite, std::filesystem::perm_options::remove);
    std::filesystem::permissions(folder, kAllWrite, std::filesystem::perm_options::remove);
    const auto failed = engine::scene::saveScene(file, second);
    std::filesystem::permissions(folder, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);
    std::filesystem::permissions(file, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);
    REQUIRE(failed.has_value());
    CHECK(failed->message.starts_with("scene 'Second' not saved: cannot "));
    CHECK(read(file) == before);
    std::size_t files = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(folder)) {
        ++files;
    }
    CHECK(files == 1);
}

TEST_CASE("A broken file next to a loaded scene leaves that scene unchanged",
          "[integration][scene]")
{
    const TempProject project("lpe_integration_scene_broken");
    const std::filesystem::path good = project.root() / "scenes" / "good.scene.json";
    const std::filesystem::path bad = project.root() / "scenes" / "bad.scene.json";
    {
        auto created = Scene::create("Good", UuidGenerator(3));
        REQUIRE(created);
        Scene scene = std::move(created).value();
        (void)scene.createEntity().value();
        REQUIRE_FALSE(engine::scene::saveScene(good, scene).has_value());
    }
    write(bad, read(good).substr(0, 60));

    auto loaded = engine::scene::loadScene(good, UuidGenerator(4));
    REQUIRE(loaded);
    Scene current = std::move(loaded).value().scene;
    const std::string before = engine::scene::sceneToJson(current);

    auto replacement = engine::scene::loadScene(bad, UuidGenerator(4));
    REQUIRE_FALSE(replacement);
    CHECK(replacement.error().message.find("malformed JSON: parse error at line") !=
          std::string::npos);
    // The caller replaces its scene only with a loaded one.
    if (replacement) {
        current = std::move(replacement).value().scene;
    }
    CHECK(engine::scene::sceneToJson(current) == before);
    CHECK(current.entityCount() == 1);
}
