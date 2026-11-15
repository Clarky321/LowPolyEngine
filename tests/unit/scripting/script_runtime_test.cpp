#include "../assets/TempProject.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scripting/ScriptRuntime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using engine::assets::AssetPath;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::core::Uuid;
using engine::core::UuidGenerator;
using engine::math::Vec3;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::Scene;
using engine::scene::SceneId;
using engine::scene::ScriptComponent;
using engine::scene::TransformComponent;
using engine::scripting::ScriptLimits;
using engine::scripting::ScriptRuntime;
using engine::scripting::ScriptStatus;
using lpe_test::assetId;

namespace {

Scene makeScene()
{
    auto created = Scene::create(SceneId{Uuid{0x0123456789ABCDEFULL, 0x8000000000000001ULL}},
                                 "Main", UuidGenerator(11));
    REQUIRE(created);
    return std::move(created).value();
}

// An entity with a transform at `position`.
EntityId addEntity(Scene& scene, Vec3 position = Vec3{0.0F})
{
    const EntityId entity = scene.createEntity().value();
    TransformComponent transform;
    transform.position = position;
    REQUIRE_FALSE(scene.addComponent(entity, transform).has_value());
    return entity;
}

ScriptRuntime makeRuntime(const ScriptLimits& limits = {})
{
    auto runtime = ScriptRuntime::create(limits);
    INFO((runtime ? std::string{} : runtime.error().message));
    REQUIRE(runtime);
    return std::move(runtime).value();
}

void addScript(ScriptRuntime& runtime, EntityId entity, std::string_view source,
               std::string_view name = "scripts/test.lua")
{
    const auto error = runtime.addScript(entity, name, source);
    INFO((error ? error->message : std::string{}));
    REQUIRE_FALSE(error.has_value());
}

Vec3 positionOf(const Scene& scene, EntityId entity)
{
    return scene.component<TransformComponent>(entity).value().position;
}

// The single error of a pass.
std::string onlyError(const std::vector<engine::core::Error>& errors)
{
    REQUIRE(errors.size() == 1);
    return errors.front().message;
}

bool contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}

constexpr std::string_view kMover = R"(
local speed = 2
return {
    start = function(entity)
        local x, y, z = engine.position(entity)
        engine.setPosition(entity, x + 1, y, z)
    end,
    update = function(entity, dt)
        local x, y, z = engine.position(entity)
        engine.setPosition(entity, x + speed * dt, y, z)
    end,
}
)";

} // namespace

TEST_CASE("A script moves its entity in start and update", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId entity = addEntity(scene, Vec3{0.0F, 5.0F, -1.0F});
    ScriptRuntime runtime = makeRuntime();
    addScript(runtime, entity, kMover);
    CHECK(runtime.instance(entity)->status == ScriptStatus::Loaded);

    CHECK(runtime.start(scene).empty());
    CHECK(runtime.instance(entity)->status == ScriptStatus::Started);
    CHECK(positionOf(scene, entity) == Vec3{1.0F, 5.0F, -1.0F});
    CHECK(runtime.update(scene, 0.5F).empty());
    CHECK(positionOf(scene, entity) == Vec3{2.0F, 5.0F, -1.0F});
    CHECK(runtime.update(scene, 0.25F).empty());
    CHECK(positionOf(scene, entity) == Vec3{2.5F, 5.0F, -1.0F});
    // start runs once.
    CHECK(runtime.start(scene).empty());
    CHECK(positionOf(scene, entity).x == 2.5F);
}

TEST_CASE("Scripts read names, transforms and the delta time", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId door = addEntity(scene, Vec3{1.0F, 2.0F, 3.0F});
    REQUIRE_FALSE(scene.addComponent(door, NameComponent{"Дверь"}).has_value());
    const EntityId nameless = addEntity(scene);
    ScriptRuntime runtime = makeRuntime();
    std::vector<std::string> printed;
    runtime.setPrintHandler([&](EntityId, std::string_view script, std::string_view text) {
        printed.push_back(std::string(script) + ": " + std::string(text));
    });
    addScript(runtime, door, R"(
return {
    start = function(entity)
        print(engine.name(entity), engine.position(entity))
        print(engine.rotation(entity))
        print(engine.scale(entity))
        print(engine.deltaTime(), engine.exists(entity),
              engine.exists("00000000-0000-4000-8000-000000000000"))
    end,
    update = function(entity, dt)
        engine.setRotation(entity, 0, 0, 1, 0)
        engine.setScale(entity, 2, 2, 2)
        print(dt, engine.deltaTime(), engine.rotation(entity))
    end,
})");
    addScript(runtime, nameless,
              "return {start = function(e) print(engine.name(e), nil, true) end}",
              "scripts/nameless.lua");

    CHECK(runtime.start(scene).empty());
    CHECK(runtime.update(scene, 0.5F).empty());
    CHECK(printed == std::vector<std::string>{
                         "scripts/test.lua: Дверь\t1.0\t2.0\t3.0",
                         "scripts/test.lua: 1.0\t0.0\t0.0\t0.0",
                         "scripts/test.lua: 1.0\t1.0\t1.0",
                         "scripts/test.lua: 0.0\ttrue\tfalse",
                         "scripts/nameless.lua: nil\tnil\ttrue",
                         "scripts/test.lua: 0.5\t0.5\t0.0\t0.0\t1.0\t0.0",
                     });
    const TransformComponent transform = scene.component<TransformComponent>(door).value();
    CHECK(transform.scale == Vec3{2.0F});
    CHECK(transform.rotation.y == 1.0F);
}

TEST_CASE("Syntax and runtime errors name the script and the line", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId broken = addEntity(scene);
    const EntityId failing = addEntity(scene);
    ScriptRuntime runtime = makeRuntime();

    const auto syntax = runtime.addScript(broken, "scripts/broken.lua", "return {\n  start = ");
    REQUIRE(syntax.has_value());
    CHECK(syntax->message.starts_with("script 'scripts/broken.lua' (entity " + broken.toString() +
                                      "): scripts/broken.lua:2: "));
    CHECK(runtime.instance(broken)->status == ScriptStatus::Failed);
    CHECK(runtime.instance(broken)->error == syntax->message);

    addScript(runtime, failing,
              "return {\n  update = function()\n    error('door jammed')\n  end\n}",
              "scripts/door.lua");
    CHECK(runtime.start(scene).empty());
    const std::string message = onlyError(runtime.update(scene, 0.1F));
    CHECK(message.starts_with("script 'scripts/door.lua' (entity " + failing.toString() +
                              "): update: scripts/door.lua:3: door jammed\nstack traceback:"));
    CHECK(contains(message, "scripts/door.lua:3: in"));
    CHECK(runtime.instance(failing)->status == ScriptStatus::Failed);
    // A failed script is not called again.
    CHECK(runtime.update(scene, 0.1F).empty());
}

TEST_CASE("Engine calls check their entity and arguments", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId self = addEntity(scene);
    const EntityId other = addEntity(scene);
    const EntityId bare = scene.createEntity().value();
    ScriptRuntime runtime = makeRuntime();

    const auto failure = [&](std::string_view call) {
        const EntityId entity = addEntity(scene);
        // The IDs reach the script through globals set at load time.
        const std::string source = "other = '" + other.toString() + "' bare = '" + bare.toString() +
                                   "'\nreturn {start = function(entity) " + std::string(call) +
                                   " end}";
        addScript(runtime, entity, source);
        const auto errors = runtime.start(scene);
        REQUIRE(errors.size() == 1);
        return errors.front().message;
    };

    CHECK(contains(failure("engine.position('00000000-0000-4000-8000-000000000001')"),
                   "engine.position: scene 'Main': entity 00000000-0000-4000-8000-000000000001: "
                   "does not exist"));
    CHECK(contains(failure("engine.position(bare)"), "engine.position: scene 'Main': entity " +
                                                         bare.toString() +
                                                         ": has no transform component"));
    CHECK(contains(failure("engine.position(42)"),
                   "bad argument #1 to 'position' (string expected, got number)"));
    CHECK(contains(failure("engine.position('door')"),
                   "bad argument #1 to 'position' (entity ID 'door': expected 36 characters"));
    CHECK(contains(failure("engine.setPosition(entity, '1', 0, 0)"),
                   "bad argument #2 to 'setPosition' (number expected, got string)"));
    CHECK(contains(failure("engine.setPosition(entity, 1, 2)"),
                   "engine.setPosition expects 4 arguments (id, x, y, z), got 3"));
    CHECK(contains(failure("engine.deltaTime(entity)"),
                   "engine.deltaTime expects 0 arguments (), got 1"));
    CHECK(contains(failure("engine.name()"), "engine.name expects 1 argument (id), got 0"));
    CHECK(contains(failure("engine.setPosition(entity, 1e300, 0, 0)"),
                   "bad argument #2 to 'setPosition' (number outside the float range)"));
    CHECK(contains(failure("engine.setPosition(entity, 0/0, 0, 0)"), "position.x is"));
    CHECK(contains(failure("engine.setPosition(entity, math.huge, 0, 0)"),
                   "transform component: position.x is inf, must be finite"));
    CHECK(contains(failure("engine.setScale(entity, 0, 1, 1)"),
                   "engine.setScale: scene 'Main': entity "));
    CHECK(contains(failure("engine.setRotation(entity, 0, 0, 0, 0)"),
                   "rotation has length 0, must be within 0.001 of 1"));
    // The rejected writes changed nothing.
    for (const auto& info : runtime.instances()) {
        if (scene.hasComponent<TransformComponent>(info.entity)) {
            CHECK(scene.component<TransformComponent>(info.entity).value() == TransformComponent{});
        }
    }
    (void)self;
}

TEST_CASE("The engine API exists only in start and update", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId entity = addEntity(scene);
    ScriptRuntime runtime = makeRuntime();
    const auto error =
        runtime.addScript(entity, "scripts/eager.lua",
                          "engine.position('00000000-0000-4000-8000-000000000001') return {}");
    REQUIRE(error.has_value());
    CHECK(contains(error->message, "engine.position: only available in start and update"));
}

TEST_CASE("Destroyed entities are errors, not crashes", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId watcher = addEntity(scene);
    const EntityId target = addEntity(scene);
    const EntityId doomed = addEntity(scene);
    ScriptRuntime runtime = makeRuntime();
    addScript(runtime, watcher,
              "local target = '" + target.toString() +
                  "'\nreturn {update = function() engine.setPosition(target, 1, 1, 1) end}");
    addScript(runtime, doomed, "return {update = function() end}", "scripts/doomed.lua");
    CHECK(runtime.start(scene).empty());
    CHECK(runtime.update(scene, 0.1F).empty());
    CHECK(positionOf(scene, target) == Vec3{1.0F});

    REQUIRE_FALSE(scene.destroyEntity(target).has_value());
    REQUIRE_FALSE(scene.destroyEntity(doomed).has_value());
    const auto errors = runtime.update(scene, 0.1F);
    REQUIRE(errors.size() == 2);
    CHECK(contains(errors[0].message, "engine.setPosition: scene 'Main': entity " +
                                          target.toString() + ": does not exist"));
    CHECK(errors[1].message == "script 'scripts/doomed.lua' (entity " + doomed.toString() +
                                   "): the entity is not in scene 'Main'");
    CHECK(runtime.instance(doomed)->status == ScriptStatus::Failed);
}

TEST_CASE("Endless loops and memory hunger are stopped", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId looping = addEntity(scene);
    const EntityId hungry = addEntity(scene);
    const EntityId moving = addEntity(scene);
    ScriptLimits limits;
    limits.instructionBudget = 200'000;
    limits.memoryLimitBytes = 4U * 1024U * 1024U;
    ScriptRuntime runtime = makeRuntime(limits);
    addScript(runtime, looping, "return {update = function() while true do end end}",
              "scripts/loop.lua");
    addScript(runtime, hungry,
              "return {update = function() local t = {} "
              "for i = 1, 1e9 do t[i] = string.rep('x', 4096) .. i end end}",
              "scripts/hungry.lua");
    addScript(runtime, moving, kMover, "scripts/mover.lua");
    CHECK(runtime.start(scene).empty());

    const auto errors = runtime.update(scene, 0.5F);
    REQUIRE(errors.size() == 2);
    CHECK(contains(errors[0].message, "scripts/loop.lua"));
    CHECK(contains(errors[0].message,
                   "instruction budget exceeded: more than 200000 instructions in one call"));
    CHECK(contains(errors[1].message, "scripts/hungry.lua"));
    CHECK(contains(errors[1].message, "not enough memory"));
    // The third script ran in the same pass, and runs on.
    CHECK(positionOf(scene, moving).x == 2.0F);
    CHECK(runtime.update(scene, 0.5F).empty());
    CHECK(positionOf(scene, moving).x == 3.0F);
    CHECK(runtime.memoryInUse() <= limits.memoryLimitBytes);
}

TEST_CASE("Scripts are limited in size, kind and result", "[scripting]")
{
    Scene scene = makeScene();
    ScriptLimits limits;
    limits.maxSourceBytes = 64;
    ScriptRuntime runtime = makeRuntime(limits);

    const EntityId big = addEntity(scene);
    const auto tooLarge = runtime.addScript(big, "scripts/big.lua", std::string(65, ' '));
    REQUIRE(tooLarge.has_value());
    CHECK(tooLarge->message ==
          "script 'scripts/big.lua' (entity " + big.toString() + "): 65 bytes, at most 64 allowed");
    CHECK_FALSE(runtime.instance(big).has_value());

    const EntityId binary = addEntity(scene);
    const auto bytecode = runtime.addScript(binary, "scripts/binary.lua",
                                            std::string("\x1bLua") + std::string(8, '\0'));
    REQUIRE(bytecode.has_value());
    CHECK(contains(bytecode->message, "attempt to load a binary chunk (mode is 't')"));

    const EntityId number = addEntity(scene);
    const auto notTable = runtime.addScript(number, "scripts/number.lua", "return 5");
    REQUIRE(notTable.has_value());
    CHECK(contains(notTable->message, "the script returned a number, not a table with start and "
                                      "update"));

    const EntityId odd = addEntity(scene);
    addScript(runtime, odd, "return {update = 5}", "scripts/odd.lua");
    CHECK(runtime.start(scene).empty());
    CHECK(contains(onlyError(runtime.update(scene, 0.1F)), "'update' is a number, not a function"));

    const auto twice = runtime.addScript(odd, "scripts/other.lua", "return {}");
    REQUIRE(twice.has_value());
    CHECK(contains(twice->message, "the entity already has the script 'scripts/odd.lua'"));
}

TEST_CASE("Scripts cannot reach each other's globals or libraries", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId first = addEntity(scene);
    const EntityId second = addEntity(scene);
    ScriptRuntime runtime = makeRuntime();
    std::vector<std::string> printed;
    runtime.setPrintHandler(
        [&](EntityId, std::string_view, std::string_view text) { printed.emplace_back(text); });
    addScript(runtime, first,
              "shared = 1 string.upper = nil engine.setPosition = nil print = nil return {}");
    addScript(runtime, second, R"(
return {start = function(entity)
    print(tostring(shared), string.upper("a"), type(engine.setPosition), type(io), type(os),
          type(require), type(load), type(dofile), type(collectgarbage), type(debug))
end})");
    CHECK(runtime.start(scene).empty());
    CHECK(printed ==
          std::vector<std::string>{"nil\tA\tfunction\tnil\tnil\tnil\tnil\tnil\tnil\tnil"});
}

TEST_CASE("A runtime restarts with all memory returned and runs scripts again", "[scripting]")
{
    Scene scene = makeScene();
    const EntityId entity = addEntity(scene);
    ScriptRuntime runtime = makeRuntime();
    addScript(runtime, entity, "return {start = function() error('first run') end}");
    CHECK(onlyError(runtime.start(scene)).find("first run") != std::string::npos);
    CHECK(runtime.instance(entity)->status == ScriptStatus::Failed);

    const std::size_t before = runtime.memoryInUse();
    CHECK(before > 0);
    // restart() checks that the closed VM returned every byte.
    REQUIRE_FALSE(runtime.restart().has_value());
    CHECK(runtime.instances().empty());
    addScript(runtime, entity, kMover);
    CHECK(runtime.start(scene).empty());
    CHECK(positionOf(scene, entity).x == 1.0F);

    ScriptRuntime moved(std::move(runtime));
    CHECK(moved.update(scene, 1.0F).empty());
    CHECK(positionOf(scene, entity).x == 3.0F);
}

TEST_CASE("Scripts of a scene load through the asset registry", "[scripting]")
{
    const lpe_test::TempProject project("lpe_scripting_load");
    project.write("scripts/mover.lua", std::string(kMover));
    project.write("scripts/huge.lua", std::string(300 * 1024, ' '));
    AssetRegistry registry(project.root());
    const auto add = [&](std::uint64_t number, AssetType type, std::string_view path) {
        REQUIRE_FALSE(registry.registerAsset(assetId(number), type, AssetPath::parse(path).value())
                          .has_value());
    };
    add(1, AssetType::Script, "scripts/mover.lua");
    add(2, AssetType::Script, "scripts/missing.lua");
    add(3, AssetType::Mesh, "models/door.obj");
    add(4, AssetType::Script, "scripts/huge.lua");

    Scene scene = makeScene();
    const auto withScript = [&](std::uint64_t number, bool enabled) {
        const EntityId entity = addEntity(scene);
        REQUIRE_FALSE(
            scene.addComponent(entity, ScriptComponent{assetId(number), enabled}).has_value());
        return entity;
    };
    const EntityId mover = withScript(1, true);
    const EntityId missing = withScript(2, true);
    const EntityId wrongType = withScript(3, true);
    const EntityId unknown = withScript(9, true);
    const EntityId huge = withScript(4, true);
    const EntityId disabled = withScript(1, false);

    ScriptRuntime runtime = makeRuntime();
    const auto errors = runtime.loadScripts(scene, registry);
    REQUIRE(errors.size() == 4);
    CHECK(contains(errors[0].message, "file not found"));
    CHECK(contains(errors[1].message, "'models/door.obj' is a mesh, expected a script"));
    CHECK(contains(errors[2].message, assetId(9).toString() + ": not registered"));
    CHECK(contains(errors[3].message, "scripts/huge.lua"));
    CHECK(contains(errors[3].message, "307200 bytes, at most 262144 allowed"));
    CHECK(runtime.instance(mover)->name == "scripts/mover.lua");
    CHECK(runtime.instance(mover)->status == ScriptStatus::Loaded);
    for (const EntityId entity : {missing, wrongType, unknown, huge}) {
        CHECK(runtime.instance(entity)->status == ScriptStatus::Failed);
    }
    CHECK_FALSE(runtime.instance(disabled).has_value());
    // Loading again adds nothing new and repeats no error.
    CHECK(runtime.loadScripts(scene, registry).empty());

    CHECK(runtime.start(scene).empty());
    CHECK(positionOf(scene, mover).x == 1.0F);
    // A disabled component pauses its script.
    REQUIRE_FALSE(scene.replaceComponent(mover, ScriptComponent{assetId(1), false}).has_value());
    CHECK(runtime.update(scene, 1.0F).empty());
    CHECK(positionOf(scene, mover).x == 1.0F);
}

TEST_CASE("Script limits and update arguments are checked", "[scripting]")
{
    ScriptLimits limits;
    CHECK_FALSE(engine::scripting::validate(limits).has_value());
    limits.maxSourceBytes = 0;
    CHECK(engine::scripting::validate(limits)->message ==
          "script limits: maxSourceBytes is 0, must be in [1, 16777216]");
    limits = {};
    limits.instructionBudget = 999;
    CHECK(engine::scripting::validate(limits)->message ==
          "script limits: instructionBudget is 999, must be at least 1000");
    limits = {};
    limits.memoryLimitBytes = 1000;
    CHECK(engine::scripting::validate(limits)->message ==
          "script limits: memoryLimitBytes is 1000, must be at least 262144");
    CHECK_FALSE(ScriptRuntime::create(limits));

    Scene scene = makeScene();
    ScriptRuntime runtime = makeRuntime();
    CHECK(onlyError(runtime.update(scene, -1.0F)) ==
          "script update: deltaSeconds is -1, must be finite and >= 0");
    CHECK(runtime.limits().maxSourceBytes == ScriptLimits{}.maxSourceBytes);
}
