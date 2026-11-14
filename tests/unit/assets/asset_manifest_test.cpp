#include "TempProject.hpp"

#include "engine/assets/AssetManifest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using engine::assets::AssetAvailability;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::assets::LoadedManifest;
using lpe_test::assetId;
using lpe_test::TempProject;

namespace {

const std::filesystem::path kRoot = std::filesystem::path("unused") / "project";

void add(AssetRegistry& registry, std::uint64_t number, AssetType type, std::string_view path)
{
    const auto error =
        registry.registerAsset(assetId(number), type, AssetPath::parse(path).value());
    INFO((error ? error->message : std::string{}));
    REQUIRE_FALSE(error.has_value());
}

LoadedManifest load(std::string_view text)
{
    auto loaded = engine::assets::manifestFromJson(text, kRoot);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded);
    return std::move(loaded).value();
}

std::string errorOf(std::string_view text)
{
    const auto loaded = engine::assets::manifestFromJson(text, kRoot);
    REQUIRE_FALSE(loaded);
    return loaded.error().message;
}

// A manifest with one asset entry given as JSON object text.
std::string withEntry(std::string_view entry)
{
    return R"({"format": "lpe.assets.manifest", "formatVersion": 1, "assets": [)" +
           std::string(entry) + "]}";
}

std::string entry(std::uint64_t number, std::string_view type, std::string_view path)
{
    return R"({"id": ")" + assetId(number).toString() + R"(", "type": ")" + std::string(type) +
           R"(", "path": ")" + std::string(path) + R"("})";
}

} // namespace

TEST_CASE("A manifest lists the assets in path order", "[assets][manifest]")
{
    AssetRegistry registry(kRoot);
    add(registry, 2, AssetType::Texture, "textures/crate.png");
    add(registry, 1, AssetType::Mesh, "models/lighthouse.obj");
    add(registry, 3, AssetType::Material, "Materials/Stone.mat");

    const std::string expected = "{\n"
                                 "  \"format\": \"lpe.assets.manifest\",\n"
                                 "  \"formatVersion\": 1,\n"
                                 "  \"assets\": [\n"
                                 "    {\n"
                                 "      \"id\": \"" +
                                 assetId(3).toString() +
                                 "\",\n"
                                 "      \"type\": \"material\",\n"
                                 "      \"path\": \"Materials/Stone.mat\"\n"
                                 "    },\n"
                                 "    {\n"
                                 "      \"id\": \"" +
                                 assetId(1).toString() +
                                 "\",\n"
                                 "      \"type\": \"mesh\",\n"
                                 "      \"path\": \"models/lighthouse.obj\"\n"
                                 "    },\n"
                                 "    {\n"
                                 "      \"id\": \"" +
                                 assetId(2).toString() +
                                 "\",\n"
                                 "      \"type\": \"texture\",\n"
                                 "      \"path\": \"textures/crate.png\"\n"
                                 "    }\n"
                                 "  ]\n"
                                 "}\n";
    CHECK(engine::assets::manifestToJson(registry) == expected);
    CHECK(engine::assets::manifestToJson(AssetRegistry(kRoot)) ==
          "{\n  \"format\": \"lpe.assets.manifest\",\n  \"formatVersion\": 1,\n  \"assets\": "
          "[]\n}\n");
}

TEST_CASE("A manifest reads back the same assets", "[assets][manifest]")
{
    AssetRegistry registry(kRoot);
    add(registry, 1, AssetType::Mesh, "models/lighthouse.obj");
    add(registry, 2, AssetType::Texture, "textures/crate.png");
    add(registry, 3, AssetType::Material, "materials/stone.mat");
    add(registry, 4, AssetType::Script, "scripts/door.lua");
    add(registry, 5, AssetType::Prefab, "prefabs/house.prefab");
    const std::string text = engine::assets::manifestToJson(registry);

    const LoadedManifest loaded = load(text);
    CHECK(loaded.warnings.empty());
    CHECK(loaded.registry.projectRoot() == kRoot);
    const auto before = registry.assets();
    const auto after = loaded.registry.assets();
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(after[i].id == before[i].id);
        CHECK(after[i].type == before[i].type);
        CHECK(after[i].path == before[i].path);
        // The files are not under the root: registered, but missing.
        CHECK(after[i].availability == AssetAvailability::Missing);
    }
    CHECK(engine::assets::manifestToJson(loaded.registry) == text);
}

TEST_CASE("A manifest without assets or with unknown fields still loads", "[assets][manifest]")
{
    const LoadedManifest empty = load(R"({"format": "lpe.assets.manifest", "formatVersion": 1})");
    CHECK(empty.registry.size() == 0);
    CHECK(empty.warnings.empty());

    const LoadedManifest extra = load(
        R"({"format": "lpe.assets.manifest", "formatVersion": 1, "comment": "hi", "assets": [)" +
        std::string(R"({"id": ")") + assetId(1).toString() +
        R"(", "type": "mesh", "path": "a.obj", "name": "Tree", "tags": []}]})");
    CHECK(extra.registry.size() == 1);
    CHECK(extra.warnings == std::vector<std::string>{"unknown field 'comment' ignored",
                                                     "unknown field 'assets[0].name' ignored",
                                                     "unknown field 'assets[0].tags' ignored"});
}

TEST_CASE("A manifest's envelope is checked", "[assets][manifest]")
{
    CHECK(errorOf("").starts_with("malformed JSON: "));
    CHECK(errorOf(R"({"format": "lpe.assets.manifest",)").starts_with("malformed JSON: "));
    CHECK(errorOf("{\n  \"format\": ]\n}").find("line 2, column 13") != std::string::npos);
    CHECK(errorOf("[1, 2]") == "expected a JSON object, found array");
    CHECK(errorOf(R"({"formatVersion": 1})") ==
          "format: missing, expected \"lpe.assets.manifest\"");
    CHECK(errorOf(R"({"format": "lpe.worldgen.settings", "formatVersion": 1})") ==
          "format: expected \"lpe.assets.manifest\", found string \"lpe.worldgen.settings\"");
    CHECK(errorOf(R"({"format": 7, "formatVersion": 1})") ==
          "format: expected \"lpe.assets.manifest\", found number 7");
    CHECK(errorOf(R"({"format": "lpe.assets.manifest"})") == "formatVersion: missing");
    CHECK(errorOf(R"({"format": "lpe.assets.manifest", "formatVersion": "1"})") ==
          "formatVersion: expected an integer, found string \"1\"");
    CHECK(errorOf(R"({"format": "lpe.assets.manifest", "formatVersion": 1.5})") ==
          "formatVersion: expected an integer, found number 1.5");
    CHECK(errorOf(R"({"format": "lpe.assets.manifest", "formatVersion": 0})") ==
          "formatVersion: 0 is not a version (the first is 1)");
    CHECK(errorOf(R"({"format": "lpe.assets.manifest", "formatVersion": 2})") ==
          "formatVersion: 2 is newer than this program reads (1); a newer version of the engine "
          "wrote the file");
    CHECK(errorOf(R"({"format": "lpe.assets.manifest", "formatVersion": 1, "assets": {}})") ==
          "assets: expected an array, found object");
}

TEST_CASE("A manifest's entries are checked field by field", "[assets][manifest]")
{
    CHECK(errorOf(withEntry("3")) == "assets[0]: expected an object, found number 3");
    CHECK(errorOf(withEntry(R"({"type": "mesh", "path": "a.obj"})")) == "assets[0].id: missing");
    CHECK(errorOf(withEntry(R"({"id": 5, "type": "mesh", "path": "a.obj"})")) ==
          "assets[0].id: expected a string, found number 5");
    CHECK(errorOf(withEntry(R"({"id": "tree", "type": "mesh", "path": "a.obj"})"))
              .starts_with("assets[0].id: asset ID 'tree': expected 36 characters"));
    CHECK(
        errorOf(withEntry(
            R"({"id": "00000000-0000-0000-0000-000000000000", "type": "mesh", "path": "a.obj"})")) ==
        "assets[0].id: asset ID '00000000-0000-0000-0000-000000000000': the null ID is not an "
        "identity");

    const std::string id = assetId(1).toString();
    CHECK(errorOf(withEntry(R"({"id": ")" + id + R"(", "path": "a.obj"})")) ==
          "assets[0].type: missing");
    CHECK(errorOf(withEntry(R"({"id": ")" + id + R"(", "type": null, "path": "a.obj"})")) ==
          "assets[0].type: expected a string, found null");
    CHECK(errorOf(withEntry(entry(1, "model", "a.obj"))) ==
          "assets[0].type: unknown asset type \"model\"; known: mesh, texture, material, script, "
          "prefab");
    CHECK(errorOf(withEntry(R"({"id": ")" + id + R"(", "type": "mesh"})")) ==
          "assets[0].path: missing");
    CHECK(errorOf(withEntry(R"({"id": ")" + id + R"(", "type": "mesh", "path": ["a"]})")) ==
          "assets[0].path: expected a string, found array");
    CHECK(errorOf(withEntry(entry(1, "mesh", "C:/models/a.obj"))) ==
          "assets[0].path: asset path 'C:/models/a.obj': a drive letter is not allowed; asset "
          "paths are relative to the project root");
    CHECK(errorOf(withEntry(entry(1, "mesh", "../a.obj"))) ==
          "assets[0].path: asset path '../a.obj': '..' leaves the project root");
    CHECK(errorOf(withEntry(entry(1, "mesh", "a.fbx"))) ==
          "assets[0]: asset " + id + ": mesh 'a.fbx' must have the extension .obj");
}

TEST_CASE("A manifest with conflicting entries is rejected as a whole", "[assets][manifest]")
{
    const std::string first = assetId(1).toString();
    const std::string second = assetId(2).toString();
    CHECK(errorOf(withEntry(entry(1, "mesh", "a.obj") + ", " + entry(1, "texture", "b.png"))) ==
          "assets[1]: asset " + first + ": already registered as mesh 'a.obj'");
    CHECK(errorOf(withEntry(entry(1, "mesh", "a.obj") + ", " + entry(2, "mesh", "./a.obj"))) ==
          "assets[1]: asset " + second + ": path 'a.obj' is already used by asset " + first);
    CHECK(errorOf(
              withEntry(entry(1, "mesh", "models/a.obj") + ", " + entry(2, "mesh", "Models/A.obj")))
              .starts_with("assets[1]: asset " + second +
                           ": path 'Models/A.obj' differs only in letter case"));
}

TEST_CASE("A manifest may list at most the asset limit", "[assets][manifest]")
{
    std::string list;
    list.reserve(2 * (engine::assets::kMaxManifestAssets + 1));
    for (std::size_t i = 0; i <= engine::assets::kMaxManifestAssets; ++i) {
        list += i == 0 ? "0" : ",0";
    }
    CHECK(errorOf(withEntry(list)) == "assets: 100001 entries, at most 100000 allowed");
}

TEST_CASE("Manifest files are saved and loaded", "[assets][manifest]")
{
    const TempProject project("lpe_assets_manifest_file");
    project.write("models/tree.obj");
    AssetRegistry registry(project.root());
    add(registry, 1, AssetType::Mesh, "models/tree.obj");
    add(registry, 2, AssetType::Texture, "textures/bark.png");
    const std::filesystem::path file = project.root() / "meta" / "assets.json";
    REQUIRE_FALSE(engine::assets::saveManifest(file, registry).has_value());

    const auto loaded = engine::assets::loadManifest(file, project.root());
    REQUIRE(loaded);
    CHECK(loaded.value().registry.find(assetId(1)).value().availability ==
          AssetAvailability::Present);
    CHECK(loaded.value().registry.find(assetId(2)).value().availability ==
          AssetAvailability::Missing);

    const auto missing = engine::assets::loadManifest(project.root() / "none.json", project.root());
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.starts_with("cannot open file '"));

    project.write("broken.json", R"({"format": "lpe.assets.manifest", "formatVersion": 9})");
    const auto broken =
        engine::assets::loadManifest(project.root() / "broken.json", project.root());
    REQUIRE_FALSE(broken);
    CHECK(broken.error().message.ends_with(
        "broken.json': formatVersion: 9 is newer than this program reads (1); a newer version of "
        "the engine wrote the file"));

    // Larger than the limit: refused before reading.
    const std::filesystem::path huge = project.root() / "huge.json";
    project.write("huge.json", "{}");
    std::filesystem::resize_file(huge, engine::assets::kMaxManifestBytes + 1);
    const auto tooLarge = engine::assets::loadManifest(huge, project.root());
    REQUIRE_FALSE(tooLarge);
    CHECK(
        tooLarge.error().message.ends_with("huge.json': 67108865 bytes, at most 67108864 allowed"));
}
