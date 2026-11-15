#pragma once

// The Lua script runtime (M4.5, ADR 0024): one sandboxed Lua 5.5 VM that runs
// the scripts of a scene's entities. No Lua type appears here; Lua stays in
// engine/src/scripting/lua/.
//
// A script is a text chunk that returns a table with the optional functions
// start(entity) and update(entity, dt); `entity` is the entity's ID as a
// string. The runtime calls start once and update whenever its owner calls
// update() (the application loop does not yet). Scripts see the engine only
// through the `engine` table:
//
//   engine.exists(id)                   -> boolean
//   engine.name(id)                     -> string, or nil without a name component
//   engine.position(id)                 -> x, y, z
//   engine.setPosition(id, x, y, z)
//   engine.rotation(id)                 -> w, x, y, z
//   engine.setRotation(id, w, x, y, z)
//   engine.scale(id)                    -> x, y, z
//   engine.setScale(id, x, y, z)
//   engine.deltaTime()                  -> seconds of the current update (0 in start)
//
// and print(...), which goes to the engine log. Writes are validated like any
// other change of a TransformComponent. Every call checks the entity in the
// scene again, so a destroyed entity is a Lua error, never a dangling access.
//
// Each call into Lua runs protected, within an instruction budget, inside a
// VM with a memory limit. A script whose loading, start or update fails is
// marked failed with its message and is not called again until the runtime
// restarts; the scene, the other scripts and the engine go on. Not
// thread-safe; owned by its caller.

#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Result.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::scripting {

struct ScriptLimits {
    // Largest script source in bytes.
    std::size_t maxSourceBytes = 256U * 1024U;
    // Lua instructions one call may run: loading a script, one start, one
    // update. Time spent inside a single library function (a long
    // string.rep, a pattern match) is not counted; memory is.
    std::uint64_t instructionBudget = 10'000'000;
    // Memory the VM holds for all scripts together.
    std::size_t memoryLimitBytes = 64U * 1024U * 1024U;
};

// maxSourceBytes in [1, 16 MiB], instructionBudget at least 1000,
// memoryLimitBytes at least 256 KiB; the error names the field.
[[nodiscard]] std::optional<core::Error> validate(const ScriptLimits& limits);

enum class ScriptStatus : std::uint8_t {
    Loaded,  // the script ran and returned its table; start has not run
    Started, // start ran (or the script has none); update is called
    Failed,  // loading, start or update failed; not called again
};

struct ScriptInstanceInfo {
    scene::EntityId entity;
    // The script's name in messages: its asset path, e.g. "scripts/door.lua".
    std::string name;
    ScriptStatus status = ScriptStatus::Loaded;
    // Why it failed, with the script's name, line and a traceback.
    std::string error;
};

// Receives what a script prints: the entity, the script's name and the text.
using PrintHandler =
    std::function<void(scene::EntityId entity, std::string_view script, std::string_view text)>;

class ScriptRuntime {
public:
    // Opens the VM. Fails for limits that do not pass validate().
    [[nodiscard]] static core::Result<ScriptRuntime> create(const ScriptLimits& limits = {});

    ScriptRuntime(ScriptRuntime&& other) noexcept;
    ScriptRuntime& operator=(ScriptRuntime&& other) noexcept;
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;
    ~ScriptRuntime();

    // Loads a script for an entity: compiles the source as text (never a
    // binary chunk), runs it in a new environment of its own and keeps the
    // table it returns. Fails for an entity that already has an instance and
    // for source above maxSourceBytes (nothing is added then); a syntax
    // error, a runtime error or a result that is not a table adds the
    // instance as failed and returns its error.
    [[nodiscard]] std::optional<core::Error>
    addScript(scene::EntityId entity, std::string_view name, std::string_view source);

    // Adds the script of every entity with an enabled ScriptComponent that has
    // no instance yet, in the scene's entity order: the asset is resolved as a
    // Script through the registry and its file read. One error per script
    // that failed (also kept as a failed instance); the others are added.
    [[nodiscard]] std::vector<core::Error> loadScripts(const scene::Scene& scene,
                                                       const assets::AssetRegistry& registry);

    // Runs start(entity) of every loaded instance, in the scene's entity
    // order. Returns the errors of the instances that failed now, including
    // instances whose entity is no longer in the scene.
    [[nodiscard]] std::vector<core::Error> start(scene::Scene& scene);

    // Runs update(entity, deltaSeconds) of every started instance, in the
    // scene's entity order, skipping entities whose ScriptComponent is
    // disabled. deltaSeconds must be finite and >= 0.
    [[nodiscard]] std::vector<core::Error> update(scene::Scene& scene, float deltaSeconds);

    [[nodiscard]] std::optional<ScriptInstanceInfo> instance(scene::EntityId entity) const;
    // All instances, ordered by entity ID.
    [[nodiscard]] std::vector<ScriptInstanceInfo> instances() const;

    // Closes the VM and opens a new one with the same limits: every instance
    // is gone and its memory returned (checked). Scripts are added again
    // afterwards.
    [[nodiscard]] std::optional<core::Error> restart();

    [[nodiscard]] std::size_t memoryInUse() const noexcept;
    [[nodiscard]] const ScriptLimits& limits() const noexcept;

    // By default print goes to the engine log (category "script").
    void setPrintHandler(PrintHandler handler);

private:
    struct Impl;
    explicit ScriptRuntime(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace engine::scripting
