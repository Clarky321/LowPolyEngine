// Scripts in a project (M4.5): a scene document with script components is
// loaded with the project's registry, the scripts are loaded through it,
// start and several updates run, and the moved transforms are saved and read
// back.

#include "engine/assets/AssetManifest.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scripting/ScriptRuntime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <utility>

using engine::assets::AssetId;
using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::core::UuidGenerator;
using engine::math::Vec3;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::Scene;
using engine::scene::ScriptComponent;
using engine::scene::TransformComponent;
using engine::scripting::ScriptRuntime;
using engine::scripting::ScriptStatus;

namespace {

class TempProject {
public:
    TempProject() : root_(std::filesystem::temp_directory_path() / "lpe_integration_scripts")
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::create_directories(root_ / "scripts");
        std::filesystem::create_directories(root_ / "scenes");
    }
    ~TempProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    void write(const std::string& relative, const std::string& text) const
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

constexpr const char* kMover = R"(-- Moves along +X at 2 units per second.
local speed = 2
return {
    update = function(entity, dt)
        local x, y, z = engine.position(entity)
        engine.setPosition(entity, x + speed * dt, y, z)
    end,
}
)";

constexpr const char* kGrower = R"(-- Starts at half size and grows by 0.25 per update.
return {
    start = function(entity)
        engine.setScale(entity, 0.5, 0.5, 0.5)
    end,
    update = function(entity)
        local x = engine.scale(entity)
        engine.setScale(entity, x + 0.25, x + 0.25, x + 0.25)
    end,
}
)";

} // namespace

TEST_CASE("Scripts of a saved scene run and their results are saved", "[integration][scripting]")
{
    const TempProject project;
    project.write("scripts/mover.lua", kMover);
    project.write("scripts/grower.lua", kGrower);
    UuidGenerator generator(1);
    const AssetId mover{generator.next()};
    const AssetId grower{generator.next()};
    {
        AssetRegistry registry(project.root());
        REQUIRE_FALSE(registry
                          .registerAsset(mover, AssetType::Script,
                                         AssetPath::parse("scripts/mover.lua").value())
                          .has_value());
        REQUIRE_FALSE(registry
                          .registerAsset(grower, AssetType::Script,
                                         AssetPath::parse("scripts/grower.lua").value())
                          .has_value());
        REQUIRE_FALSE(
            engine::assets::saveManifest(project.root() / "assets.json", registry).has_value());
    }

    const std::filesystem::path file = project.root() / "scenes" / "level.scene.json";
    EntityId cart;
    EntityId plant;
    EntityId paused;
    {
        Scene scene = Scene::create("Level", generator).value();
        cart = scene.createEntity().value();
        REQUIRE_FALSE(scene.addComponent(cart, NameComponent{"Cart"}).has_value());
        TransformComponent start;
        start.position = {10.0F, 0.0F, -2.0F};
        REQUIRE_FALSE(scene.addComponent(cart, start).has_value());
        REQUIRE_FALSE(scene.addComponent(cart, ScriptComponent{mover, true}).has_value());
        plant = scene.createEntity().value();
        REQUIRE_FALSE(scene.addComponent(plant, TransformComponent{}).has_value());
        REQUIRE_FALSE(scene.addComponent(plant, ScriptComponent{grower, true}).has_value());
        paused = scene.createEntity().value();
        REQUIRE_FALSE(scene.addComponent(paused, TransformComponent{}).has_value());
        REQUIRE_FALSE(scene.addComponent(paused, ScriptComponent{mover, false}).has_value());
        REQUIRE_FALSE(engine::scene::saveScene(file, scene).has_value());
    }

    const auto manifest =
        engine::assets::loadManifest(project.root() / "assets.json", project.root());
    REQUIRE(manifest);
    const AssetRegistry& registry = manifest.value().registry;
    auto loaded = engine::scene::loadScene(file, UuidGenerator(2), &registry);
    REQUIRE(loaded);
    Scene scene = std::move(loaded).value().scene;

    auto created = ScriptRuntime::create();
    REQUIRE(created);
    ScriptRuntime runtime = std::move(created).value();
    CHECK(runtime.loadScripts(scene, registry).empty());
    CHECK(runtime.instance(cart)->name == "scripts/mover.lua");
    CHECK(runtime.instance(plant)->name == "scripts/grower.lua");
    CHECK_FALSE(runtime.instance(paused).has_value());
    CHECK(runtime.start(scene).empty());
    for (int frame = 0; frame < 4; ++frame) {
        CHECK(runtime.update(scene, 0.5F).empty());
    }
    CHECK(runtime.instance(cart)->status == ScriptStatus::Started);

    REQUIRE_FALSE(engine::scene::saveScene(file, scene).has_value());
    auto reloaded = engine::scene::loadScene(file, UuidGenerator(3), &registry);
    REQUIRE(reloaded);
    const Scene& result = reloaded.value().scene;
    CHECK(result.component<TransformComponent>(cart).value().position == Vec3{14.0F, 0.0F, -2.0F});
    CHECK(result.component<TransformComponent>(plant).value().scale == Vec3{1.5F});
    CHECK(result.component<TransformComponent>(paused).value() == TransformComponent{});
    CHECK(result.component<ScriptComponent>(cart).value() == ScriptComponent{mover, true});
    CHECK(result.component<NameComponent>(cart).value().value == "Cart");
}
