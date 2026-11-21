// A project on disk (engine/project/Project.hpp, M5.4, ADR 0028): a new
// project's exact layout, the round trip, a moved folder, each part missing
// or broken (all or nothing, the file named), warnings with their files, and
// saves refused before anything is written.

#include "ProjectTestSupport.hpp"

#include "engine/project/Project.hpp"
#include "engine/project/ProjectDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using engine::core::UuidGenerator;
using engine::project::createProject;
using engine::project::openProject;
using engine::project::saveProject;
using lpe_test::filesBelow;
using lpe_test::fileText;
using lpe_test::kDefaultFiles;
using lpe_test::projectVersion;
using lpe_test::TempProject;
using lpe_test::textsOf;
using lpe_test::textsOnDisk;

namespace {

bool startsWith(const std::string& text, std::string_view prefix)
{
    return text.starts_with(prefix);
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

std::string openError(const std::filesystem::path& root)
{
    const auto opened = openProject(root, UuidGenerator{7});
    REQUIRE_FALSE(opened.hasValue());
    return opened.error().message;
}

constexpr std::string_view kNewDocument = R"({
  "format": "lpe.project",
  "formatVersion": 1,
  "name": "Sky Islands",
  "assetManifest": "assets.json",
  "startScene": "scenes/main.scene.json",
  "worldSettings": "worlds/world.json"
}
)";

constexpr std::string_view kEmptyManifest = R"({
  "format": "lpe.assets.manifest",
  "formatVersion": 1,
  "assets": []
}
)";

} // namespace

TEST_CASE("A new project has its exact layout and opens", "[project]")
{
    const TempProject folder("lpe_project_new");
    // The project's folder does not exist yet: it is created.
    const std::filesystem::path root = folder.root() / "Sky Islands";
    const auto created = createProject(root, "Sky Islands", UuidGenerator{1});
    REQUIRE(created.hasValue());

    CHECK(filesBelow(root) == kDefaultFiles);
    CHECK(fileText(root / "project.json") == kNewDocument);
    CHECK(fileText(root / "assets.json") == kEmptyManifest);
    // The default world settings, as shipped in assets/worlds/default.json.
    CHECK(fileText(root / "worlds" / "world.json") ==
          fileText(std::filesystem::path(LPE_ASSET_DIRECTORY) / "worlds" / "default.json"));
    const engine::project::Project& project = created.value();
    CHECK(project.startScene.name() == "Main");
    CHECK_FALSE(project.startScene.id().isNull());
    CHECK(project.startScene.entityCount() == 0U);
    CHECK(project.assets.size() == 0U);
    CHECK(project.root == root);

    const auto opened = openProject(root, UuidGenerator{2});
    REQUIRE(opened.hasValue());
    CHECK(opened.value().warnings.empty());
    CHECK(textsOf(opened.value().project) == textsOf(project));
    CHECK(opened.value().project.startScene.id() == project.startScene.id());

    // Two new projects get different scene IDs.
    const auto other = createProject(folder.root() / "Other", "Other", UuidGenerator{3});
    REQUIRE(other.hasValue());
    CHECK(other.value().startScene.id() != project.startScene.id());
}

TEST_CASE("A new project never overwrites a file", "[project]")
{
    const TempProject folder("lpe_project_create_refused");
    const std::filesystem::path root = folder.root();
    REQUIRE(createProject(root, "First", UuidGenerator{1}).hasValue());
    const auto before = textsOnDisk(root);
    const auto again = createProject(root, "Second", UuidGenerator{2});
    REQUIRE_FALSE(again.hasValue());
    CHECK(contains(again.error().message, "already holds a project"));
    CHECK(textsOnDisk(root) == before);

    // A folder with one of the default files, but no project.
    const TempProject other("lpe_project_create_refused_file");
    other.write("worlds/world.json", "mine");
    const auto refused = createProject(other.root(), "P", UuidGenerator{1});
    REQUIRE_FALSE(refused.hasValue());
    CHECK(contains(refused.error().message, "'worlds/world.json' already exists"));
    CHECK(filesBelow(other.root()) == std::vector<std::string>{"worlds/world.json"});
    CHECK(fileText(other.root() / "worlds" / "world.json") == "mine");

    // An invalid name, and a file where the folder should be.
    const TempProject third("lpe_project_create_refused_name");
    CHECK_FALSE(createProject(third.root() / "p", "", UuidGenerator{1}).hasValue());
    CHECK_FALSE(std::filesystem::exists(third.root() / "p"));
    third.write("file", "x");
    const auto onFile = createProject(third.root() / "file", "P", UuidGenerator{1});
    REQUIRE_FALSE(onFile.hasValue());
    CHECK(contains(onFile.error().message, "it is a file"));
}

TEST_CASE("A saved project opens with every part as it was saved", "[project]")
{
    const TempProject folder("lpe_project_round_trip");
    const std::filesystem::path root = folder.root() / "p";
    REQUIRE(createProject(root, "P", UuidGenerator{1}).hasValue());
    const engine::project::Project project = projectVersion(folder, root, 3);
    const auto saved = saveProject(project);
    REQUIRE(saved.hasValue());
    CHECK(saved.value().warnings.empty());
    CHECK(textsOnDisk(root) == textsOf(project));

    const auto opened = openProject(root, UuidGenerator{9});
    REQUIRE(opened.hasValue());
    CHECK(opened.value().warnings.empty());
    CHECK(textsOf(opened.value().project) == textsOf(project));
    CHECK(opened.value().project.startScene.entityCount() == 3U);
    CHECK(opened.value().project.worldSettings.seed == 1003U);

    // Saved again unchanged: the same bytes, no file more.
    REQUIRE(saveProject(opened.value().project).hasValue());
    CHECK(textsOnDisk(root) == textsOf(project));
    std::vector<std::string> expected = kDefaultFiles;
    expected.insert(expected.end(), {"models/v3-1.obj", "models/v3-2.obj", "models/v3-3.obj"});
    std::sort(expected.begin(), expected.end());
    CHECK(filesBelow(root) == expected);
}

TEST_CASE("A moved project opens the same", "[project]")
{
    const TempProject folder("lpe_project_moved");
    const std::filesystem::path before = folder.root() / "before";
    REQUIRE(createProject(before, "P", UuidGenerator{1}).hasValue());
    const engine::project::Project project = projectVersion(folder, before, 2);
    REQUIRE(saveProject(project).hasValue());

    const std::filesystem::path after = folder.root() / "somewhere" / "else";
    std::filesystem::create_directories(after.parent_path());
    std::filesystem::rename(before, after);

    const auto opened = openProject(after, UuidGenerator{4});
    REQUIRE(opened.hasValue());
    const engine::project::Project& moved = opened.value().project;
    CHECK(textsOf(moved) == textsOf(project));
    CHECK(moved.root == after);
    CHECK(moved.assets.projectRoot() == after);
    for (const engine::assets::AssetMetadata& asset : moved.assets.assets()) {
        INFO(asset.path.text());
        CHECK(moved.assets.resolve(asset.id, engine::assets::AssetType::Mesh).hasValue());
    }
    // Saved in the new place: the same bytes there.
    REQUIRE(saveProject(moved).hasValue());
    CHECK(textsOnDisk(after) == textsOf(project));
}

TEST_CASE("A missing or broken part opens nothing and names its file", "[project]")
{
    const TempProject folder("lpe_project_broken");
    const std::filesystem::path root = folder.root();
    REQUIRE(createProject(root, "P", UuidGenerator{1}).hasValue());
    const engine::project::Project project = projectVersion(folder, root, 2);
    REQUIRE(saveProject(project).hasValue());
    const lpe_test::ProjectTexts good = textsOnDisk(root);
    const auto restore = [&] {
        folder.write("project.json", good.document);
        folder.write("assets.json", good.manifest);
        folder.write("scenes/main.scene.json", good.scene);
        folder.write("worlds/world.json", good.world);
        REQUIRE(openProject(root, UuidGenerator{1}).hasValue());
    };

    SECTION("Missing parts")
    {
        for (const std::string_view part :
             {"assets.json", "scenes/main.scene.json", "worlds/world.json"}) {
            INFO(part);
            folder.remove(part);
            const std::string error = openError(root);
            CHECK(startsWith(error, std::string(part) + ": "));
            CHECK(contains(error, "missing"));
            restore();
        }
        folder.remove("project.json");
        CHECK(contains(openError(root), "holds no project: 'project.json' is missing"));
        restore();
        CHECK(contains(openError(root / "nothing-here"), "is not a folder"));
    }

    SECTION("Broken parts")
    {
        for (const std::string_view part :
             {"project.json", "assets.json", "scenes/main.scene.json", "worlds/world.json"}) {
            INFO(part);
            folder.write(part, "{\"format\": ");
            std::string error = openError(root);
            CHECK(startsWith(error, std::string(part) + ": "));
            CHECK(contains(error, "malformed JSON"));
            // Another document in its place.
            folder.write(part, part == "project.json" ? good.manifest : good.document);
            error = openError(root);
            CHECK(startsWith(error, std::string(part) + ": "));
            restore();
        }
        // World settings that do not pass validate().
        engine::worldgen::WorldGenSettings invalid;
        invalid.islands.count = -5;
        folder.write("worlds/world.json", engine::worldgen::settingsToJson(invalid));
        CHECK(startsWith(openError(root), "worlds/world.json: islands.count"));
        restore();
    }

    SECTION("A scene that refers to an asset the manifest does not have")
    {
        engine::project::Project fewer = projectVersion(folder, root, 2);
        const auto assets = fewer.assets.assets();
        REQUIRE_FALSE(fewer.assets.unregisterAsset(assets.back().id).has_value());
        folder.write("assets.json", engine::assets::manifestToJson(fewer.assets));
        const std::string error = openError(root);
        CHECK(startsWith(error, "scenes/main.scene.json: "));
        restore();
    }

    SECTION("An asset at the path of one of the project's files")
    {
        std::string manifest = good.manifest;
        const std::string entry =
            "{\"id\": \"0b5e7000-0000-4000-8000-000000000009\", \"type\": \"prefab\", \"path\": "
            "\"worlds/world.json\"}, ";
        manifest.insert(manifest.find('[') + 1, entry);
        folder.write("assets.json", manifest);
        const std::string error = openError(root);
        CHECK(error == "assets.json: the asset 'worlds/world.json' has the path of the world "
                       "settings");
        restore();
    }
}

TEST_CASE("Warnings of a project name their files", "[project]")
{
    const TempProject folder("lpe_project_warnings");
    const std::filesystem::path root = folder.root();
    REQUIRE(createProject(root, "P", UuidGenerator{1}).hasValue());
    const lpe_test::ProjectTexts texts = textsOnDisk(root);
    const auto addField = [](std::string text) {
        text.insert(text.find('{') + 1, "\"extra\": 1, ");
        return text;
    };
    folder.write("project.json", addField(texts.document));
    folder.write("assets.json", addField(texts.manifest));
    folder.write("scenes/main.scene.json", addField(texts.scene));
    folder.write("worlds/world.json", addField(texts.world));

    const auto opened = openProject(root, UuidGenerator{1});
    REQUIRE(opened.hasValue());
    const std::vector<std::string> expected{
        "project.json: unknown field 'extra' ignored",
        "assets.json: unknown field 'extra' ignored",
        "scenes/main.scene.json: unknown field 'extra' ignored",
        "worlds/world.json: unknown field 'extra' ignored",
    };
    CHECK(opened.value().warnings == expected);
}

TEST_CASE("A save that would not open is refused before anything is written", "[project]")
{
    const TempProject folder("lpe_project_save_refused");
    const std::filesystem::path root = folder.root();
    REQUIRE(createProject(root, "P", UuidGenerator{1}).hasValue());
    REQUIRE(saveProject(projectVersion(folder, root, 1)).hasValue());
    const lpe_test::ProjectTexts before = textsOnDisk(root);
    const auto refused = [&](const engine::project::Project& project, std::string_view part) {
        // Taken here: building the project wrote its model files.
        const std::vector<std::string> files = filesBelow(root);
        const auto saved = saveProject(project);
        REQUIRE_FALSE(saved.hasValue());
        INFO(saved.error().message);
        CHECK(startsWith(saved.error().message, "cannot save the project: "));
        CHECK(contains(saved.error().message, part));
        CHECK(contains(saved.error().message, "nothing was written"));
        CHECK(textsOnDisk(root) == before);
        CHECK(filesBelow(root) == files);
    };

    SECTION("Invalid world settings")
    {
        engine::project::Project project = projectVersion(folder, root, 2);
        project.worldSettings.islands.count = -1;
        refused(project, "worlds/world.json: ");
    }
    SECTION("A scene that refers to an unknown asset")
    {
        engine::project::Project project = projectVersion(folder, root, 2);
        const auto assets = project.assets.assets();
        REQUIRE_FALSE(project.assets.unregisterAsset(assets.front().id).has_value());
        refused(project, "scenes/main.scene.json: ");
    }
    SECTION("An invalid document")
    {
        engine::project::Project project = projectVersion(folder, root, 2);
        project.document.worldSettings = project.document.startScene;
        refused(project, "project.json: worldSettings: ");
    }
    SECTION("An asset at a part's path")
    {
        engine::project::Project project = projectVersion(folder, root, 2);
        REQUIRE_FALSE(project.assets
                          .registerAsset(lpe_test::assetId(77), engine::assets::AssetType::Prefab,
                                         project.document.startScene)
                          .has_value());
        refused(project, "the asset 'scenes/main.scene.json' has the path of the start scene");
    }
}
