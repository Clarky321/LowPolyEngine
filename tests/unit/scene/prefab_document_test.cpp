#include "../assets/TempProject.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/PrefabDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::scene::LoadedPrefab;
using engine::scene::NameComponent;
using engine::scene::Prefab;
using engine::scene::PrefabEntity;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;
using lpe_test::assetId;

namespace {

// A tower: a base (local ID 5) and a top (local ID 2) whose parent is the base.
Prefab tower()
{
    Prefab prefab;
    prefab.id = assetId(20);
    prefab.name = "Tower";
    PrefabEntity base;
    base.localId = 5;
    base.components.name = NameComponent{"Base"};
    base.components.staticMesh = StaticMeshComponent{assetId(1), {}};
    PrefabEntity top;
    top.localId = 2;
    top.parent = 5;
    TransformComponent transform;
    transform.position = {0.0F, 4.5F, 0.0F};
    top.components.transform = transform;
    prefab.entities = {base, top};
    return prefab;
}

LoadedPrefab load(std::string_view text, const AssetRegistry* registry = nullptr)
{
    auto loaded = engine::scene::prefabFromJson(text, registry);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded);
    return std::move(loaded).value();
}

std::string errorOf(std::string_view text, const AssetRegistry* registry = nullptr)
{
    const auto loaded = engine::scene::prefabFromJson(text, registry);
    REQUIRE_FALSE(loaded);
    return loaded.error().message;
}

std::string document(std::string_view entities)
{
    return R"({"format": "lpe.prefab", "formatVersion": 1, "id": ")" + assetId(20).toString() +
           R"(", "name": "Tower", "entities": [)" + std::string(entities) + "]}";
}

AssetRegistry registryWith(const std::filesystem::path& root)
{
    AssetRegistry registry(root);
    REQUIRE_FALSE(registry
                      .registerAsset(assetId(1), AssetType::Mesh,
                                     AssetPath::parse("models/tower.obj").value())
                      .has_value());
    REQUIRE_FALSE(registry
                      .registerAsset(assetId(20), AssetType::Prefab,
                                     AssetPath::parse("prefabs/tower.prefab").value())
                      .has_value());
    return registry;
}

} // namespace

TEST_CASE("A prefab document has a fixed layout", "[scene][prefab][document]")
{
    const std::string expected = "{\n"
                                 "  \"format\": \"lpe.prefab\",\n"
                                 "  \"formatVersion\": 1,\n"
                                 "  \"id\": \"" +
                                 assetId(20).toString() +
                                 "\",\n"
                                 "  \"name\": \"Tower\",\n"
                                 "  \"entities\": [\n"
                                 "    {\n"
                                 "      \"localId\": 5,\n"
                                 "      \"components\": {\n"
                                 "        \"name\": {\n"
                                 "          \"value\": \"Base\"\n"
                                 "        },\n"
                                 "        \"staticMesh\": {\n"
                                 "          \"mesh\": \"" +
                                 assetId(1).toString() +
                                 "\",\n"
                                 "          \"material\": null\n"
                                 "        }\n"
                                 "      }\n"
                                 "    },\n"
                                 "    {\n"
                                 "      \"localId\": 2,\n"
                                 "      \"components\": {\n"
                                 "        \"transform\": {\n"
                                 "          \"position\": {\n"
                                 "            \"x\": 0.0,\n"
                                 "            \"y\": 4.5,\n"
                                 "            \"z\": 0.0\n"
                                 "          },\n"
                                 "          \"rotation\": {\n"
                                 "            \"w\": 1.0,\n"
                                 "            \"x\": 0.0,\n"
                                 "            \"y\": 0.0,\n"
                                 "            \"z\": 0.0\n"
                                 "          },\n"
                                 "          \"scale\": {\n"
                                 "            \"x\": 1.0,\n"
                                 "            \"y\": 1.0,\n"
                                 "            \"z\": 1.0\n"
                                 "          }\n"
                                 "        },\n"
                                 "        \"parent\": {\n"
                                 "          \"entity\": 5\n"
                                 "        }\n"
                                 "      }\n"
                                 "    }\n"
                                 "  ]\n"
                                 "}\n";
    CHECK(engine::scene::prefabToJson(tower()) == expected);
}

TEST_CASE("A prefab document reads back the same prefab", "[scene][prefab][document]")
{
    const std::string text = engine::scene::prefabToJson(tower());
    const LoadedPrefab loaded = load(text);
    CHECK(loaded.prefab == tower());
    CHECK(loaded.warnings.empty());
    CHECK(engine::scene::prefabToJson(loaded.prefab) == text);
}

TEST_CASE("A prefab document's local IDs and links are checked", "[scene][prefab][document]")
{
    CHECK(errorOf(document(R"({"components": {}})")) == "entities[0].localId: missing");
    CHECK(errorOf(document(R"({"localId": 0})")) == "entities[0].localId: 0, must be at least 1");
    CHECK(errorOf(document(R"({"localId": "1"})")) ==
          "entities[0].localId: expected an integer, found string \"1\"");
    CHECK(errorOf(document(R"({"localId": 1.5})")) ==
          "entities[0].localId: expected an integer, found number 1.5");
    CHECK(errorOf(document(R"({"localId": 1}, {"localId": 1})")) ==
          "entities[1].localId: 1 is also entities[0]");
    CHECK(errorOf(document(R"({"localId": 1, "components": {"parent": {"entity": 7}}})")) ==
          "prefab 'Tower': entities[0].components.parent.entity: 7 is not the local ID of an "
          "entity of the prefab");
    CHECK(errorOf(document(R"({"localId": 1, "components": {"parent": {"entity": 1}}})")) ==
          "prefab 'Tower': entities[0].components.parent.entity: an entity cannot be its own "
          "parent");
    CHECK(errorOf(document(R"({"localId": 1, "components": {"parent": {"entity": 2}}},
                              {"localId": 2, "components": {"parent": {"entity": 1}}})"))
              .ends_with("which makes a cycle"));
    CHECK(errorOf(document(R"({"localId": 1, "components": {"parent": {"entity": "x"}}})")) ==
          "entities[0].components.parent.entity: expected an integer, found string \"x\"");
    CHECK(errorOf(document(R"({"localId": 1, "components": {"prefabInstance": {}}})")) ==
          "entities[0].components.prefabInstance: unknown component type; known: name, "
          "transform, staticMesh, camera, directionalLight, script, parent");
    CHECK(errorOf(document("")) == "prefab 'Tower': entities: a prefab needs at least one entity");
    CHECK(errorOf(R"({"format": "lpe.scene", "formatVersion": 1})") ==
          "format: expected \"lpe.prefab\", found string \"lpe.scene\"");
    CHECK(errorOf(R"({"format": "lpe.prefab", "formatVersion": 2})")
              .starts_with("formatVersion: 2 is newer than this program reads (1)"));
    CHECK(errorOf("{").starts_with("malformed JSON: parse error at line 1"));
    CHECK(errorOf(R"({"format": "lpe.prefab", "formatVersion": 1, "name": "Tower"})") ==
          "id: missing");
}

TEST_CASE("A prefab document is checked against a registry", "[scene][prefab][document]")
{
    const AssetRegistry registry = registryWith("unused");
    CHECK(load(engine::scene::prefabToJson(tower()), &registry).prefab == tower());

    Prefab unknownMesh = tower();
    unknownMesh.entities[0].components.staticMesh = StaticMeshComponent{assetId(9), {}};
    CHECK(errorOf(engine::scene::prefabToJson(unknownMesh), &registry) ==
          "entities[0].components.staticMesh.mesh: asset " + assetId(9).toString() +
              ": not registered");

    Prefab wrongId = tower();
    wrongId.id = assetId(1);
    CHECK(errorOf(engine::scene::prefabToJson(wrongId), &registry) ==
          "id: asset " + assetId(1).toString() +
              ": 'models/tower.obj' is a mesh, expected a prefab");
}

TEST_CASE("Prefab files are saved, loaded and found through the registry",
          "[scene][prefab][document]")
{
    const lpe_test::TempProject project("lpe_prefab_documents");
    std::filesystem::create_directories(project.root() / "prefabs");
    const std::filesystem::path file = project.root() / "prefabs" / "tower.prefab";
    REQUIRE_FALSE(engine::scene::savePrefab(file, tower()).has_value());
    const AssetRegistry registry = registryWith(project.root());

    const auto loaded = engine::scene::loadPrefabAsset(registry, assetId(20));
    REQUIRE(loaded);
    CHECK(loaded.value().prefab == tower());

    // Unknown or wrongly typed assets.
    CHECK(engine::scene::loadPrefabAsset(registry, assetId(30)).error().message ==
          "asset " + assetId(30).toString() + ": not registered");
    CHECK(engine::scene::loadPrefabAsset(registry, assetId(1)).error().message ==
          "asset " + assetId(1).toString() + ": 'models/tower.obj' is a mesh, expected a prefab");

    // A document whose ID is another registered prefab.
    AssetRegistry other = registryWith(project.root());
    REQUIRE_FALSE(other
                      .registerAsset(assetId(21), AssetType::Prefab,
                                     AssetPath::parse("prefabs/copy.prefab").value())
                      .has_value());
    std::filesystem::copy_file(file, project.root() / "prefabs" / "copy.prefab");
    CHECK(engine::scene::loadPrefabAsset(other, assetId(21))
              .error()
              .message.ends_with("copy.prefab': the document's ID " + assetId(20).toString() +
                                 " is not the asset ID " + assetId(21).toString() +
                                 " the registry lists for the file"));

    // A broken document, and a prefab that is not saved.
    project.write("prefabs/tower.prefab", "{\"format\": \"lpe.prefab\"");
    CHECK(engine::scene::loadPrefabAsset(registry, assetId(20))
              .error()
              .message.find("tower.prefab': malformed JSON") != std::string::npos);
    Prefab empty = tower();
    empty.entities.clear();
    const auto refused = engine::scene::savePrefab(file, empty);
    REQUIRE(refused.has_value());
    CHECK(refused->message.ends_with("not written: prefab 'Tower': entities: a prefab needs at "
                                     "least one entity"));
}
