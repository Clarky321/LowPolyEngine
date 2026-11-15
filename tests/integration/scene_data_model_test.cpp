// The M4 exit criterion end to end (M4.7). A project with a mesh, a material,
// scripts and a prefab; a scene with every component type, asset references
// through the project's registry and a prefab made from three of its
// entities. The scene is saved, destroyed and loaded with every ID, component
// value and asset reference intact; its Lua scripts run, a failing one
// contained; the prefab is instantiated twice; the scene is saved and loaded
// again. Every ID comes from a seeded generator, so the final document is the
// same bytes on every platform (a golden hash).

#include "engine/assets/AssetManifest.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/geometry/ObjImport.hpp"
#include "engine/scene/AssetReferences.hpp"
#include "engine/scene/EntityComponents.hpp"
#include "engine/scene/Prefab.hpp"
#include "engine/scene/PrefabDocument.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scripting/ScriptRuntime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using engine::assets::AssetAvailability;
using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::core::UuidGenerator;
using engine::math::Vec3;
using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::EntityComponents;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::Prefab;
using engine::scene::PrefabInstance;
using engine::scene::PrefabInstanceComponent;
using engine::scene::Scene;
using engine::scene::ScriptComponent;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;
using engine::scripting::ScriptRuntime;
using engine::scripting::ScriptStatus;

namespace {

class TempProject {
public:
    TempProject() : root_(std::filesystem::temp_directory_path() / "lpe_integration_data_model")
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        for (const char* folder : {"models", "scripts", "prefabs", "scenes"}) {
            std::filesystem::create_directories(root_ / folder);
        }
    }
    ~TempProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    void write(const std::string& relative, std::string_view text) const
    {
        REQUIRE_FALSE(engine::core::writeBinaryFile(
                          root_ / std::filesystem::path(relative),
                          std::span<const std::uint8_t>(
                              reinterpret_cast<const std::uint8_t*>(text.data()), text.size()))
                          .has_value());
    }

private:
    std::filesystem::path root_;
};

// Raises its entity by one unit per second; start records its name.
constexpr std::string_view kKeeper = R"(local name
return {
    start = function(entity)
        name = engine.name(entity)
        print("keeping " .. name)
    end,
    update = function(entity, dt)
        local x, y, z = engine.position(entity)
        engine.setPosition(entity, x, y + dt, z)
    end,
}
)";

// Tries to flatten its entity to a scale of zero, which validation rejects.
constexpr std::string_view kBroken = R"(return {
    update = function(entity)
        engine.setScale(entity, 0, 1, 1)
    end,
}
)";

// FNV-1a, 64 bits: a fingerprint of a document's bytes.
std::uint64_t fingerprint(std::string_view text)
{
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

// The final document's fingerprint, recorded on Windows and checked on Linux
// by CI: scene IDs, entity IDs, instance IDs, floats and text are written the
// same everywhere.
constexpr std::uint64_t kFinalDocumentFingerprint = 0x5C1189269DA14440ULL;

TransformComponent transformAt(Vec3 position)
{
    TransformComponent transform;
    transform.position = position;
    return transform;
}

// Every entity's components, its parent link and its instance link, in the
// scene's order: what a round trip must keep.
struct EntitySnapshot {
    EntityId id;
    EntityComponents components;
    std::optional<EntityId> parent;
    std::optional<PrefabInstanceComponent> instance;

    friend bool operator==(const EntitySnapshot&, const EntitySnapshot&) = default;
};

std::vector<EntitySnapshot> snapshot(const Scene& scene)
{
    std::vector<EntitySnapshot> result;
    for (const EntityId id : scene.entityIds()) {
        EntitySnapshot entity{id, engine::scene::componentsOf(scene, id), std::nullopt,
                              std::nullopt};
        if (auto parent = scene.component<ParentComponent>(id)) {
            entity.parent = parent.value().entity;
        }
        if (auto instance = scene.component<PrefabInstanceComponent>(id)) {
            entity.instance = instance.value();
        }
        result.push_back(std::move(entity));
    }
    return result;
}

} // namespace

TEST_CASE("A scene with every M4 system survives save, load, scripts and prefabs",
          "[integration][scene][assets][scripting][prefab]")
{
    const TempProject project;
    std::filesystem::copy_file(std::filesystem::path(LPE_ASSET_DIRECTORY) / "models" /
                                   "lighthouse.obj",
                               project.root() / "models" / "lighthouse.obj");
    project.write("scripts/keeper.lua", kKeeper);
    project.write("scripts/broken.lua", kBroken);

    UuidGenerator assetIds(41);
    const AssetId mesh{assetIds.next()};
    const AssetId material{assetIds.next()};
    const AssetId keeper{assetIds.next()};
    const AssetId broken{assetIds.next()};
    const AssetId house{assetIds.next()};
    const std::filesystem::path manifestFile = project.root() / "assets.json";
    const std::filesystem::path sceneFile = project.root() / "scenes" / "harbor.scene.json";

    // 1. The project's assets. Materials have no files before M6: the
    // material is registered and missing on disk, which is a state, not an
    // error.
    AssetRegistry authoring(project.root());
    REQUIRE_FALSE(
        authoring
            .registerAsset(mesh, AssetType::Mesh, AssetPath::parse("models/lighthouse.obj").value())
            .has_value());
    REQUIRE_FALSE(authoring
                      .registerAsset(material, AssetType::Material,
                                     AssetPath::parse("materials/stone.mat").value())
                      .has_value());
    REQUIRE_FALSE(authoring
                      .registerAsset(keeper, AssetType::Script,
                                     AssetPath::parse("scripts/keeper.lua").value())
                      .has_value());
    REQUIRE_FALSE(authoring
                      .registerAsset(broken, AssetType::Script,
                                     AssetPath::parse("scripts/broken.lua").value())
                      .has_value());

    // 2. The scene, its entities and components.
    std::string saved;
    std::vector<EntitySnapshot> before;
    Prefab housePrefab;
    engine::scene::SceneId sceneId;
    EntityId lighthouse;
    EntityId sign;
    EntityId buoy;
    {
        Scene harbor = Scene::create("Harbor — Nord", UuidGenerator(42)).value();
        sceneId = harbor.id();

        lighthouse = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(lighthouse, NameComponent{"Lighthouse"}).has_value());
        TransformComponent tower = transformAt({12.5F, 3.25F, -40.0F});
        tower.rotation = engine::math::Quat(0.9238795F, 0.0F, 0.3826834F, 0.0F);
        tower.scale = {1.0F, 1.5F, 1.0F};
        REQUIRE_FALSE(harbor.addComponent(lighthouse, tower).has_value());
        REQUIRE_FALSE(
            harbor.addComponent(lighthouse, StaticMeshComponent{mesh, material}).has_value());

        const EntityId sun = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(sun, NameComponent{"Sun"}).has_value());
        REQUIRE_FALSE(harbor
                          .addComponent(sun, DirectionalLightComponent{{-0.3F, -1.0F, -0.2F},
                                                                       {1.0F, 0.95F, 0.85F},
                                                                       1.25F})
                          .has_value());

        const EntityId eye = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(eye, NameComponent{"Main camera"}).has_value());
        REQUIRE_FALSE(harbor.addComponent(eye, transformAt({0.0F, 8.0F, 25.0F})).has_value());
        REQUIRE_FALSE(harbor.addComponent(eye, CameraComponent{0.9F, 0.05F, 2500.0F}).has_value());

        const EntityId crate = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(crate, NameComponent{"Ящик"}).has_value());
        REQUIRE_FALSE(harbor.addComponent(crate, transformAt({-3.0F, 0.0F, 1.5F})).has_value());
        // No material: the renderer's default material.
        REQUIRE_FALSE(harbor.addComponent(crate, StaticMeshComponent{mesh, {}}).has_value());

        sign = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(sign, NameComponent{"Broken sign"}).has_value());
        REQUIRE_FALSE(harbor.addComponent(sign, TransformComponent{}).has_value());
        REQUIRE_FALSE(harbor.addComponent(sign, ScriptComponent{broken, true}).has_value());

        buoy = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(buoy, transformAt({4.0F, -0.5F, 9.0F})).has_value());
        REQUIRE_FALSE(harbor.addComponent(buoy, ScriptComponent{keeper, false}).has_value());

        // A house of three entities, linked base ← roof ← chimney.
        const EntityId base = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(base, NameComponent{"House"}).has_value());
        REQUIRE_FALSE(harbor.addComponent(base, transformAt({30.0F, 0.0F, 30.0F})).has_value());
        REQUIRE_FALSE(harbor.addComponent(base, StaticMeshComponent{mesh, material}).has_value());
        const EntityId roof = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(roof, NameComponent{"Roof"}).has_value());
        REQUIRE_FALSE(harbor.addComponent(roof, transformAt({0.0F, 3.0F, 0.0F})).has_value());
        REQUIRE_FALSE(harbor.addComponent(roof, StaticMeshComponent{mesh, {}}).has_value());
        REQUIRE_FALSE(harbor.addComponent(roof, ParentComponent{base}).has_value());
        const EntityId chimney = harbor.createEntity().value();
        REQUIRE_FALSE(harbor.addComponent(chimney, NameComponent{"Chimney"}).has_value());
        REQUIRE_FALSE(harbor.addComponent(chimney, transformAt({0.5F, 1.0F, 0.0F})).has_value());
        REQUIRE_FALSE(harbor.addComponent(chimney, ParentComponent{roof}).has_value());

        // 3. The prefab made from the house, saved and registered.
        const std::array<EntityId, 3> parts{base, roof, chimney};
        auto created = engine::scene::createPrefab(harbor, parts, house, "House");
        REQUIRE(created);
        housePrefab = std::move(created).value();
        REQUIRE_FALSE(
            engine::scene::savePrefab(project.root() / "prefabs" / "house.prefab", housePrefab)
                .has_value());
        REQUIRE_FALSE(authoring
                          .registerAsset(house, AssetType::Prefab,
                                         AssetPath::parse("prefabs/house.prefab").value())
                          .has_value());
        REQUIRE_FALSE(engine::assets::saveManifest(manifestFile, authoring).has_value());

        // 4. A script component added after the prefab, then the scene saved.
        REQUIRE_FALSE(harbor.addComponent(lighthouse, ScriptComponent{keeper, true}).has_value());
        CHECK(engine::scene::findAssetReferenceProblems(harbor, authoring).empty());
        before = snapshot(harbor);
        saved = engine::scene::sceneToJson(harbor);
        REQUIRE_FALSE(engine::scene::saveScene(sceneFile, harbor).has_value());
    } // 5. The runtime scene is destroyed.
    REQUIRE(before.size() == 9);

    // 6. The project opened again: manifest, then the scene with its checks.
    const auto manifest = engine::assets::loadManifest(manifestFile, project.root());
    REQUIRE(manifest);
    CHECK(manifest.value().warnings.empty());
    const AssetRegistry& registry = manifest.value().registry;
    CHECK(registry.size() == 5);
    auto loaded = engine::scene::loadScene(sceneFile, UuidGenerator(43), &registry);
    REQUIRE(loaded);
    CHECK(loaded.value().warnings.empty());
    Scene harbor = std::move(loaded).value().scene;

    // Every ID and component value is back, bit for bit, and the document
    // reads back to the same bytes.
    CHECK(harbor.id() == sceneId);
    CHECK(harbor.name() == "Harbor — Nord");
    CHECK(snapshot(harbor) == before);
    CHECK(engine::scene::sceneToJson(harbor) == saved);

    // Every asset reference names a registered asset of the right type.
    CHECK(engine::scene::findAssetReferenceProblems(harbor, registry).empty());
    const StaticMeshComponent towerMesh = harbor.component<StaticMeshComponent>(lighthouse).value();
    CHECK(towerMesh.mesh == mesh);
    CHECK(towerMesh.material == material);
    CHECK(registry.find(material).value().availability == AssetAvailability::Missing);
    const auto meshFile = registry.resolve(towerMesh.mesh, AssetType::Mesh);
    REQUIRE(meshFile);
    const auto meshData = engine::geometry::readObj(meshFile.value());
    REQUIRE(meshData);
    CHECK_FALSE(meshData.value().mesh.indices.empty());
    CHECK(registry.find(harbor.component<ScriptComponent>(lighthouse).value().asset)
              .value()
              .path.text() == "scripts/keeper.lua");

    // 7. The scripts run: the keeper raises the lighthouse, the broken
    // script fails once and is contained, the disabled one never loads.
    auto runtime = ScriptRuntime::create();
    REQUIRE(runtime);
    std::vector<std::string> printed;
    runtime.value().setPrintHandler([&printed](EntityId, std::string_view, std::string_view text) {
        printed.emplace_back(text);
    });
    CHECK(runtime.value().loadScripts(harbor, registry).empty());
    CHECK(runtime.value().instances().size() == 2);
    CHECK_FALSE(runtime.value().instance(buoy).has_value());
    CHECK(runtime.value().start(harbor).empty());
    CHECK(printed == std::vector<std::string>{"keeping Lighthouse"});
    const auto failures = runtime.value().update(harbor, 0.5F);
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].message.find("scripts/broken.lua") != std::string::npos);
    CHECK(failures[0].message.find("engine.setScale") != std::string::npos);
    for (int frame = 0; frame < 3; ++frame) {
        CHECK(runtime.value().update(harbor, 0.5F).empty());
    }
    CHECK(runtime.value().instance(lighthouse)->status == ScriptStatus::Started);
    CHECK(runtime.value().instance(sign)->status == ScriptStatus::Failed);
    CHECK(harbor.component<TransformComponent>(lighthouse).value().position ==
          Vec3{12.5F, 5.25F, -40.0F});
    CHECK(harbor.component<TransformComponent>(sign).value() == TransformComponent{});
    CHECK(harbor.component<TransformComponent>(buoy).value() == transformAt({4.0F, -0.5F, 9.0F}));

    // 8. The prefab, loaded through its asset ID, instantiated twice.
    const auto loadedPrefab = engine::scene::loadPrefabAsset(registry, house);
    REQUIRE(loadedPrefab);
    CHECK(loadedPrefab.value().prefab == housePrefab);
    const auto first = engine::scene::instantiatePrefab(harbor, loadedPrefab.value().prefab);
    const auto second = engine::scene::instantiatePrefab(harbor, loadedPrefab.value().prefab);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.value().id != second.value().id);
    std::set<EntityId> ids;
    for (const EntityId id : harbor.entityIds()) {
        ids.insert(id);
    }
    CHECK(ids.size() == 15);
    REQUIRE_FALSE(
        harbor.replaceComponent(second.value().entities[0], transformAt({-30.0F, 0.0F, 30.0F}))
            .has_value());
    CHECK(engine::scene::findAssetReferenceProblems(harbor, registry).empty());

    // 9. Saved and loaded again: links and instance data are kept.
    REQUIRE_FALSE(engine::scene::saveScene(sceneFile, harbor).has_value());
    auto reloaded = engine::scene::loadScene(sceneFile, UuidGenerator(44), &registry);
    REQUIRE(reloaded);
    const Scene& result = reloaded.value().scene;
    CHECK(snapshot(result) == snapshot(harbor));
    const std::string document = engine::scene::sceneToJson(result);
    CHECK(document == engine::scene::sceneToJson(harbor));
    for (const PrefabInstance* instance : {&first.value(), &second.value()}) {
        REQUIRE(instance->entities.size() == 3);
        for (std::size_t index = 0; index < 3; ++index) {
            const EntityId entity = instance->entities[index];
            CHECK(result.component<PrefabInstanceComponent>(entity).value() ==
                  PrefabInstanceComponent{house, instance->id,
                                          static_cast<engine::scene::PrefabLocalId>(index + 1)});
            CHECK(result.component<NameComponent>(entity).value() ==
                  housePrefab.entities[index].components.name.value());
        }
        CHECK(result.component<ParentComponent>(instance->entities[1]).value().entity ==
              instance->entities[0]);
        CHECK(result.component<ParentComponent>(instance->entities[2]).value().entity ==
              instance->entities[1]);
        CHECK_FALSE(result.hasComponent<ParentComponent>(instance->entities[0]));
    }
    CHECK(result.component<TransformComponent>(first.value().entities[0]).value().position ==
          Vec3{30.0F, 0.0F, 30.0F});
    CHECK(result.component<TransformComponent>(second.value().entities[0]).value().position ==
          Vec3{-30.0F, 0.0F, 30.0F});

    INFO(document);
    CHECK(fingerprint(document) == kFinalDocumentFingerprint);
}
