#include "../assets/TempProject.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/SceneDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::core::Uuid;
using engine::core::UuidGenerator;
using engine::math::Quat;
using engine::math::Vec3;
using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::EntityId;
using engine::scene::LoadedScene;
using engine::scene::NameComponent;
using engine::scene::Scene;
using engine::scene::SceneId;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;
using lpe_test::assetId;

namespace {

const SceneId kMainId{Uuid{0x0123456789ABCDEFULL, 0x8000000000000001ULL}};
const std::string kMainIdText = "01234567-89ab-cdef-8000-000000000001";
// Golden IDs of UuidGenerator(1) (tests/unit/core/uuid_test.cpp).
const std::string kFirstEntity = "b3f2af6d-0fc7-40c5-853b-559647364cea";
const std::string kSecondEntity = "92f89756-082a-4514-a42e-1c7bc266a3a7";

Scene makeScene(std::string_view name = "Main")
{
    auto created = Scene::create(kMainId, name, UuidGenerator(1));
    REQUIRE(created);
    return std::move(created).value();
}

EntityId makeEntity(Scene& scene)
{
    auto created = scene.createEntity();
    REQUIRE(created);
    return created.value();
}

template <typename T>
void add(Scene& scene, EntityId entity, const T& component)
{
    const auto error = scene.addComponent(entity, component);
    INFO((error ? error->message : std::string{}));
    REQUIRE_FALSE(error.has_value());
}

LoadedScene load(std::string_view text, const AssetRegistry* registry = nullptr)
{
    auto loaded = engine::scene::sceneFromJson(text, UuidGenerator(99), registry);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded);
    return std::move(loaded).value();
}

std::string errorOf(std::string_view text, const AssetRegistry* registry = nullptr)
{
    const auto loaded = engine::scene::sceneFromJson(text, UuidGenerator(99), registry);
    REQUIRE_FALSE(loaded);
    return loaded.error().message;
}

const std::string kEnvelope = R"("format": "lpe.scene", "formatVersion": 1, )";

// A document with the given entities (JSON array elements).
std::string withEntities(std::string_view entities)
{
    return "{" + kEnvelope + R"("id": ")" + kMainIdText + R"(", "name": "Main", "entities": [)" +
           std::string(entities) + "]}";
}

// A document with one entity that has the given components (JSON object
// members).
std::string withComponents(std::string_view components)
{
    return withEntities(R"({"id": ")" + kFirstEntity + R"(", "components": {)" +
                        std::string(components) + "}}");
}

bool sameBits(float a, float b)
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool sameBits(const TransformComponent& a, const TransformComponent& b)
{
    const float left[] = {a.position.x, a.position.y, a.position.z, a.rotation.w, a.rotation.x,
                          a.rotation.y, a.rotation.z, a.scale.x,    a.scale.y,    a.scale.z};
    const float right[] = {b.position.x, b.position.y, b.position.z, b.rotation.w, b.rotation.x,
                           b.rotation.y, b.rotation.z, b.scale.x,    b.scale.y,    b.scale.z};
    for (std::size_t i = 0; i < std::size(left); ++i) {
        if (!sameBits(left[i], right[i])) {
            return false;
        }
    }
    return true;
}

// A scene with every component type, unusual floats and a gap in the order.
Scene richScene()
{
    Scene scene = makeScene("Маяк — main ✓");
    const EntityId lighthouse = makeEntity(scene);
    add(scene, lighthouse, NameComponent{"Lighthouse «Север»"});
    TransformComponent transform;
    transform.position = {0.1F, -0.0F, 1.0e-40F};
    transform.rotation = Quat{0.70710677F, 0.0F, 0.70710677F, 0.0F};
    transform.scale = {1.0F / 3.0F, 2.5F, 1.0e-4F};
    add(scene, lighthouse, transform);
    add(scene, lighthouse, StaticMeshComponent{assetId(1), assetId(2)});

    const EntityId removed = makeEntity(scene);
    const EntityId empty = makeEntity(scene);
    (void)empty;
    const EntityId sun = makeEntity(scene);
    add(scene, sun, NameComponent{"Sun"});
    add(scene, sun, DirectionalLightComponent{{0.0F, -1.0F, 0.0F}, {1.0F, 0.9F, 0.75F}, 3.5F});
    add(scene, sun, StaticMeshComponent{assetId(1), {}});

    const EntityId eye = makeEntity(scene);
    add(scene, eye, CameraComponent{0.8F, 0.05F, 2500.0F});
    TransformComponent eyeTransform;
    eyeTransform.position = {-99999.99F, 12345.678F, 3.14159274F};
    add(scene, eye, eyeTransform);

    REQUIRE_FALSE(scene.destroyEntity(removed).has_value());
    return scene;
}

template <typename T>
void checkSame(const Scene& a, const Scene& b, EntityId entity)
{
    REQUIRE(a.hasComponent<T>(entity) == b.hasComponent<T>(entity));
    if (a.hasComponent<T>(entity)) {
        CHECK(a.component<T>(entity).value() == b.component<T>(entity).value());
    }
}

} // namespace

TEST_CASE("A scene document has a fixed layout", "[scene][document]")
{
    Scene scene = makeScene();
    const EntityId first = makeEntity(scene);
    (void)makeEntity(scene);
    add(scene, first, NameComponent{"Маяк"});
    TransformComponent transform;
    transform.position = {1.5F, -2.0F, 0.1F};
    add(scene, first, transform);
    add(scene, first, StaticMeshComponent{assetId(1), {}});
    add(scene, first, CameraComponent{});
    add(scene, first, DirectionalLightComponent{});

    const std::string expected = "{\n"
                                 "  \"format\": \"lpe.scene\",\n"
                                 "  \"formatVersion\": 1,\n"
                                 "  \"id\": \"" +
                                 kMainIdText +
                                 "\",\n"
                                 "  \"name\": \"Main\",\n"
                                 "  \"entities\": [\n"
                                 "    {\n"
                                 "      \"id\": \"" +
                                 kFirstEntity +
                                 "\",\n"
                                 "      \"components\": {\n"
                                 "        \"name\": {\n"
                                 "          \"value\": \"Маяк\"\n"
                                 "        },\n"
                                 "        \"transform\": {\n"
                                 "          \"position\": {\n"
                                 "            \"x\": 1.5,\n"
                                 "            \"y\": -2.0,\n"
                                 "            \"z\": 0.1\n"
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
                                 "        \"staticMesh\": {\n"
                                 "          \"mesh\": \"" +
                                 assetId(1).toString() +
                                 "\",\n"
                                 "          \"material\": null\n"
                                 "        },\n"
                                 "        \"camera\": {\n"
                                 "          \"verticalFovRadians\": 1.0471976,\n"
                                 "          \"nearPlane\": 0.1,\n"
                                 "          \"farPlane\": 1000.0\n"
                                 "        },\n"
                                 "        \"directionalLight\": {\n"
                                 "          \"direction\": {\n"
                                 "            \"x\": 0.0,\n"
                                 "            \"y\": -1.0,\n"
                                 "            \"z\": 0.0\n"
                                 "          },\n"
                                 "          \"color\": {\n"
                                 "            \"r\": 1.0,\n"
                                 "            \"g\": 1.0,\n"
                                 "            \"b\": 1.0\n"
                                 "          },\n"
                                 "          \"intensity\": 1.0\n"
                                 "        }\n"
                                 "      }\n"
                                 "    },\n"
                                 "    {\n"
                                 "      \"id\": \"" +
                                 kSecondEntity +
                                 "\",\n"
                                 "      \"components\": {}\n"
                                 "    }\n"
                                 "  ]\n"
                                 "}\n";
    CHECK(engine::scene::sceneToJson(scene) == expected);
    CHECK(engine::scene::sceneToJson(makeScene()) ==
          "{\n  \"format\": \"lpe.scene\",\n  \"formatVersion\": 1,\n  \"id\": \"" + kMainIdText +
              "\",\n  \"name\": \"Main\",\n  \"entities\": []\n}\n");
}

TEST_CASE("A scene reads back logically identical", "[scene][document]")
{
    const Scene original = richScene();
    const std::string text = engine::scene::sceneToJson(original);
    const LoadedScene loaded = load(text);
    const Scene& copy = loaded.scene;
    CHECK(loaded.warnings.empty());

    CHECK(copy.id() == original.id());
    CHECK(copy.name() == original.name());
    CHECK(copy.entityCount() == original.entityCount());
    // The same IDs in the same order: loading creates no IDs of its own.
    REQUIRE(copy.entityIds() == original.entityIds());
    for (const EntityId entity : original.entityIds()) {
        checkSame<NameComponent>(original, copy, entity);
        checkSame<TransformComponent>(original, copy, entity);
        checkSame<StaticMeshComponent>(original, copy, entity);
        checkSame<CameraComponent>(original, copy, entity);
        checkSame<DirectionalLightComponent>(original, copy, entity);
        if (original.hasComponent<TransformComponent>(entity)) {
            CHECK(sameBits(original.component<TransformComponent>(entity).value(),
                           copy.component<TransformComponent>(entity).value()));
        }
    }
    const EntityId lighthouse = original.entityIds().front();
    CHECK(std::signbit(copy.component<TransformComponent>(lighthouse).value().position.y));
    CHECK(copy.component<TransformComponent>(lighthouse).value().position.z == 1.0e-40F);

    // Written again: the same bytes.
    CHECK(engine::scene::sceneToJson(copy) == text);
    CHECK(engine::scene::sceneToJson(original) == text);
}

TEST_CASE("A loaded scene makes new entity IDs with the given generator", "[scene][document]")
{
    LoadedScene loaded = load(engine::scene::sceneToJson(richScene()));
    const std::vector<EntityId> before = loaded.scene.entityIds();
    const auto added = loaded.scene.createEntity();
    REQUIRE(added);
    for (const EntityId entity : before) {
        CHECK(entity != added.value());
    }
    CHECK(loaded.scene.entityIds().back() == added.value());
}

TEST_CASE("Missing optional fields take their defaults; unknown fields are warned about",
          "[scene][document]")
{
    const LoadedScene minimal =
        load("{" + kEnvelope + R"("id": ")" + kMainIdText + R"(", "name": "Main"})");
    CHECK(minimal.scene.entityCount() == 0);

    const LoadedScene partial = load(withComponents(
        R"("transform": {"position": {"y": 2}}, "camera": {}, "directionalLight": {"intensity": 2},
           "staticMesh": {"mesh": ")" +
        assetId(1).toString() + R"("})"));
    const EntityId entity = partial.scene.entityIds().front();
    TransformComponent transform;
    transform.position.y = 2.0F;
    CHECK(partial.scene.component<TransformComponent>(entity).value() == transform);
    CHECK(partial.scene.component<CameraComponent>(entity).value() == CameraComponent{});
    CHECK(partial.scene.component<DirectionalLightComponent>(entity).value().intensity == 2.0F);
    CHECK(partial.scene.component<StaticMeshComponent>(entity).value().material.isNull());
    CHECK(partial.warnings.empty());

    const LoadedScene noComponents = load(withEntities(R"({"id": ")" + kFirstEntity + R"("})"));
    CHECK(noComponents.scene.entityCount() == 1);

    const LoadedScene extra = load(
        "{" + kEnvelope + R"("id": ")" + kMainIdText +
        R"(", "name": "Main", "author": "me", "entities": [{"id": ")" + kFirstEntity +
        R"(", "locked": true, "components": {"transform": {"pivot": 1, "position": {"x": 1, "w": 2}},
           "name": {"value": "a", "note": ""}}}]})");
    CHECK(extra.warnings ==
          std::vector<std::string>{"unknown field 'author' ignored",
                                   "unknown field 'entities[0].locked' ignored",
                                   "unknown field 'entities[0].components.name.note' ignored",
                                   "unknown field 'entities[0].components.transform.pivot' ignored",
                                   "unknown field 'entities[0].components.transform.position.w' "
                                   "ignored"});
}

TEST_CASE("A scene document's envelope is checked", "[scene][document]")
{
    CHECK(errorOf("").starts_with("malformed JSON: parse error at line 1, column 1"));
    CHECK(errorOf("{\n  \"format\": \"lpe.scene\",\n  \"formatVersion\": 1,\n}")
              .starts_with("malformed JSON: parse error at line 4, column 1"));
    CHECK(errorOf("[]") == "expected a JSON object, found array");
    CHECK(errorOf(R"({"formatVersion": 1})") == "format: missing, expected \"lpe.scene\"");
    CHECK(errorOf(R"({"format": "lpe.assets.manifest", "formatVersion": 1})") ==
          "format: expected \"lpe.scene\", found string \"lpe.assets.manifest\"");
    CHECK(errorOf(R"({"format": "lpe.scene"})") == "formatVersion: missing");
    CHECK(errorOf(R"({"format": "lpe.scene", "formatVersion": 0})") ==
          "formatVersion: 0 is not a version (the first is 1)");
    CHECK(errorOf(R"({"format": "lpe.scene", "formatVersion": 2})") ==
          "formatVersion: 2 is newer than this program reads (1); a newer version of the engine "
          "wrote the file");
    CHECK(errorOf(R"({"format": "lpe.scene", "formatVersion": "1"})") ==
          "formatVersion: expected an integer, found string \"1\"");

    const std::string start = "{" + kEnvelope;
    CHECK(errorOf(start + R"("name": "Main"})") == "id: missing");
    CHECK(errorOf(start + R"("id": 7, "name": "Main"})") ==
          "id: expected a string, found number 7");
    CHECK(errorOf(start + R"("id": "main", "name": "Main"})")
              .starts_with("id: scene ID 'main': expected 36 characters"));
    CHECK(errorOf(start + R"("id": "00000000-0000-0000-0000-000000000000", "name": "Main"})") ==
          "id: scene ID '00000000-0000-0000-0000-000000000000': the null ID is not an identity");
    CHECK(errorOf(start + R"("id": ")" + kMainIdText + R"("})") == "name: missing");
    CHECK(errorOf(start + R"("id": ")" + kMainIdText + R"(", "name": ""})") ==
          "name: must not be empty");
    CHECK(errorOf(start + R"("id": ")" + kMainIdText + R"(", "name": "a\tb"})") ==
          "name: control character U+0009 at byte 1");
    CHECK(errorOf(start + R"("id": ")" + kMainIdText + R"(", "name": ")" + std::string(257, 'n') +
                  R"("})") == "name: 257 bytes, at most 256 allowed");
    CHECK(load(start + R"("id": ")" + kMainIdText + R"(", "name": ")" + std::string(256, 'n') +
               R"("})")
              .scene.name()
              .size() == 256);
    CHECK(errorOf(start + R"("id": ")" + kMainIdText + R"(", "name": "Main", "entities": {}})") ==
          "entities: expected an array, found object");
}

TEST_CASE("A scene document's entities are checked", "[scene][document]")
{
    CHECK(errorOf(withEntities("1")) == "entities[0]: expected an object, found number 1");
    CHECK(errorOf(withEntities("{}")) == "entities[0].id: missing");
    CHECK(errorOf(withEntities(R"({"id": null})")) ==
          "entities[0].id: expected a string, found null");
    CHECK(errorOf(withEntities(R"({"id": "x"})"))
              .starts_with("entities[0].id: entity ID 'x': expected 36 characters"));
    const std::string first = R"({"id": ")" + kFirstEntity + R"("})";
    const std::string second = R"({"id": ")" + kSecondEntity + R"("})";
    CHECK(errorOf(withEntities(first + ", " + second + ", " + first)) ==
          "entities[2].id: entity " + kFirstEntity + " is also entities[0]");
    CHECK(errorOf(withEntities(R"({"id": ")" + kFirstEntity + R"(", "components": []})")) ==
          "entities[0].components: expected an object, found array");
    CHECK(errorOf(withComponents(R"("script": {"path": "door.lua"})")) ==
          "entities[0].components.script: unknown component type; known: name, transform, "
          "staticMesh, camera, directionalLight");
    CHECK(errorOf(withComponents(R"("camera": 5)")) ==
          "entities[0].components.camera: expected an object, found number 5");
}

TEST_CASE("A scene document's component values are checked", "[scene][document]")
{
    CHECK(errorOf(withComponents(R"("name": {})")) == "entities[0].components.name.value: missing");
    CHECK(errorOf(withComponents(R"("name": {"value": 3})")) ==
          "entities[0].components.name.value: expected a string, found number 3");
    CHECK(errorOf(withComponents(R"("name": {"value": ""})")) ==
          "entities[0].components.name: value: must not be empty");

    CHECK(errorOf(withComponents(R"("transform": {"position": [1, 2, 3]})")) ==
          "entities[0].components.transform.position: expected an object, found array");
    CHECK(errorOf(withComponents(R"("transform": {"position": {"x": "1"}})")) ==
          "entities[0].components.transform.position.x: expected a number, found string \"1\"");
    CHECK(errorOf(withComponents(R"("transform": {"position": {"x": 1e39}})")) ==
          "entities[0].components.transform.position.x: 1e+39 is outside the range of a float");
    CHECK(errorOf(withComponents(R"("transform": {"position": {"x": 100001}})")) ==
          "entities[0].components.transform: position.x is 100001, must be in [-100000, 100000]");
    CHECK(errorOf(withComponents(R"("transform": {"scale": {"y": 0}})")) ==
          "entities[0].components.transform: scale.y is 0, must be in [0.0001, 10000]");
    CHECK(errorOf(withComponents(R"("transform": {"rotation": {"w": 2}})")) ==
          "entities[0].components.transform: rotation has length 2, must be within 0.001 of 1");

    CHECK(errorOf(withComponents(R"("staticMesh": {})")) ==
          "entities[0].components.staticMesh.mesh: missing");
    CHECK(errorOf(withComponents(R"("staticMesh": {"mesh": null})")) ==
          "entities[0].components.staticMesh.mesh: expected a string, found null");
    CHECK(errorOf(withComponents(
              R"("staticMesh": {"mesh": "00000000-0000-0000-0000-000000000000"})")) ==
          "entities[0].components.staticMesh.mesh: asset ID "
          "'00000000-0000-0000-0000-000000000000': the null ID is not an identity");
    CHECK(errorOf(withComponents(R"("staticMesh": {"mesh": ")" + assetId(1).toString() +
                                 R"(", "material": 4})")) ==
          "entities[0].components.staticMesh.material: expected a string, found number 4");
    CHECK(errorOf(withComponents(R"("staticMesh": {"mesh": ")" + assetId(1).toString() +
                                 R"(", "material": "stone"})"))
              .starts_with("entities[0].components.staticMesh.material: asset ID 'stone':"));

    CHECK(errorOf(withComponents(R"("camera": {"nearPlane": 0})")) ==
          "entities[0].components.camera: nearPlane is 0, must be at least 0.001");
    CHECK(errorOf(withComponents(R"("camera": {"farPlane": true})")) ==
          "entities[0].components.camera.farPlane: expected a number, found boolean true");
    CHECK(errorOf(withComponents(R"("directionalLight": {"intensity": -1})")) ==
          "entities[0].components.directionalLight: intensity is -1, must be at least 0");
    CHECK(
        errorOf(withComponents(R"("directionalLight": {"direction": {"x": 0, "y": 0, "z": 0}})")) ==
        "entities[0].components.directionalLight: direction is (0, 0, 0), must not be zero");
    CHECK(errorOf(withComponents(R"("directionalLight": {"color": {"g": -0.5}})")) ==
          "entities[0].components.directionalLight: color.g is -0.5, must be at least 0");
}

TEST_CASE("Asset references are checked against a registry when one is given", "[scene][document]")
{
    AssetRegistry registry(std::filesystem::path("unused"));
    REQUIRE_FALSE(registry.registerAsset(assetId(1), AssetType::Mesh,
                                         AssetPath::parse("models/tree.obj").value()));
    REQUIRE_FALSE(registry.registerAsset(assetId(3), AssetType::Texture,
                                         AssetPath::parse("textures/bark.png").value()));
    REQUIRE_FALSE(registry.registerAsset(assetId(4), AssetType::Material,
                                         AssetPath::parse("materials/bark.mat").value()));

    const auto mesh = [](std::uint64_t meshNumber, std::string_view material) {
        return withComponents(R"("staticMesh": {"mesh": ")" + assetId(meshNumber).toString() +
                              R"(", "material": )" + std::string(material) + "}");
    };
    const std::string bark = "\"" + assetId(4).toString() + "\"";
    CHECK(load(mesh(1, bark), &registry).scene.entityCount() == 1);
    CHECK(load(mesh(1, "null"), &registry).scene.entityCount() == 1);
    CHECK(errorOf(mesh(9, "null"), &registry) == "entities[0].components.staticMesh.mesh: asset " +
                                                     assetId(9).toString() + ": not registered");
    CHECK(errorOf(mesh(3, "null"), &registry) ==
          "entities[0].components.staticMesh.mesh: asset " + assetId(3).toString() +
              ": 'textures/bark.png' is a texture, expected a mesh");
    CHECK(errorOf(mesh(1, "\"" + assetId(3).toString() + "\""), &registry) ==
          "entities[0].components.staticMesh.material: asset " + assetId(3).toString() +
              ": 'textures/bark.png' is a texture, expected a material");
    // Without a registry the references are kept as they are, unchecked.
    const LoadedScene unchecked = load(mesh(9, "null"));
    CHECK(unchecked.scene.component<StaticMeshComponent>(unchecked.scene.entityIds().front())
              .value()
              .mesh == assetId(9));
}

TEST_CASE("Scene documents have entity and size limits", "[scene][document]")
{
    std::string atLimit;
    atLimit.reserve(engine::scene::kMaxSceneEntities * 52);
    UuidGenerator generator(5);
    for (std::size_t i = 0; i < engine::scene::kMaxSceneEntities; ++i) {
        atLimit += i == 0 ? "" : ",";
        atLimit += R"({"id": ")" + engine::core::toString(generator.next()) + R"("})";
    }
    const LoadedScene full = load(withEntities(atLimit));
    CHECK(full.scene.entityCount() == engine::scene::kMaxSceneEntities);

    CHECK(errorOf(withEntities(atLimit + R"(, {"id": ")" +
                               engine::core::toString(generator.next()) + R"("})")) ==
          "entities: 100001 entries, at most 100000 allowed");

    const std::string huge(engine::scene::kMaxSceneDocumentBytes + 1, ' ');
    CHECK(errorOf(huge) == "document: 67108865 bytes, at most 67108864 allowed");
}

TEST_CASE("Scene document files are saved and loaded", "[scene][document]")
{
    const lpe_test::TempProject project("lpe_scene_document_files");
    const std::filesystem::path file = project.root() / "main.scene.json";
    const Scene original = richScene();
    REQUIRE_FALSE(engine::scene::saveScene(file, original).has_value());
    const auto loaded = engine::scene::loadScene(file, UuidGenerator(2));
    REQUIRE(loaded);
    CHECK(loaded.value().scene.entityIds() == original.entityIds());
    CHECK(engine::scene::sceneToJson(loaded.value().scene) == engine::scene::sceneToJson(original));

    // Saving again replaces the file.
    Scene renamed = makeScene("Renamed");
    REQUIRE_FALSE(engine::scene::saveScene(file, renamed).has_value());
    CHECK(engine::scene::loadScene(file, UuidGenerator(2)).value().scene.name() == "Renamed");

    const auto missingFolder =
        engine::scene::saveScene(project.root() / "nowhere" / "a.json", original);
    REQUIRE(missingFolder.has_value());
    CHECK(missingFolder->message.starts_with("scene 'Маяк — main ✓' not saved: cannot write '"));
    CHECK(missingFolder->message.ends_with("' does not exist"));

    Scene moved = makeScene();
    Scene target(std::move(moved));
    // NOLINTNEXTLINE(bugprone-use-after-move): saving a moved-from scene is refused.
    const auto movedFrom = engine::scene::saveScene(project.root() / "moved.json", moved);
    REQUIRE(movedFrom.has_value());
    CHECK(movedFrom->message.ends_with("': it has no ID (a moved-from scene)"));

    const auto missing = engine::scene::loadScene(project.root() / "none.json", UuidGenerator(2));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.starts_with("cannot open file '"));
    const auto folder = engine::scene::loadScene(project.root(), UuidGenerator(2));
    REQUIRE_FALSE(folder);
    CHECK(folder.error().message.ends_with("' is not a regular file"));

    project.write("broken.json", "{\"format\": \"lpe.scene\", \"formatVersion\": 1, \"id\": 1}");
    const auto broken = engine::scene::loadScene(project.root() / "broken.json", UuidGenerator(2));
    REQUIRE_FALSE(broken);
    CHECK(broken.error().message.ends_with("broken.json': id: expected a string, found number 1"));

    project.write("huge.json", "{}");
    std::filesystem::resize_file(project.root() / "huge.json",
                                 engine::scene::kMaxSceneDocumentBytes + 1);
    const auto tooLarge = engine::scene::loadScene(project.root() / "huge.json", UuidGenerator(2));
    REQUIRE_FALSE(tooLarge);
    CHECK(
        tooLarge.error().message.ends_with("huge.json': 67108865 bytes, at most 67108864 allowed"));
}
