#pragma once

// A scene: the entities of one game world document (M4.1, ADR 0020).
//
// Entities are identified only by their persistent EntityId; the scene hands
// out no pointer or reference into its storage, so a destroyed entity cannot
// be reached through stale data: every operation on an ID the scene does not
// contain fails with an error that names the scene and the ID.
//
// A scene is a value owned by its caller (no global registry), move-only so
// that entity IDs are not duplicated by accident, and not thread-safe: it is
// used by one thread at a time. It needs no window or GPU.

#include "engine/core/Result.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/SceneId.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::scene {

// Longest name in bytes (UTF-8).
inline constexpr std::size_t kMaxNameBytes = 256;

// Generated IDs that are already in the scene are drawn again, at most this
// often (with 122 random bits it never happens in practice).
inline constexpr int kMaxIdAttempts = 16;

// Checks a name of the scene model (a scene's name now): not empty, at most
// kMaxNameBytes bytes, valid UTF-8, no control characters (U+0000-U+001F,
// U+007F-U+009F). The error starts with `what`, e.g.
// "scene name: invalid UTF-8 at byte 3".
[[nodiscard]] std::optional<core::Error> validateName(std::string_view name, std::string_view what);

class Scene {
public:
    // A scene with the given ID and name. Fails for a null ID or an invalid
    // name. The generator makes the IDs of entities created without one.
    [[nodiscard]] static core::Result<Scene> create(SceneId sceneId, std::string_view name,
                                                    core::UuidGenerator generator);
    // The same with an ID drawn from the generator.
    [[nodiscard]] static core::Result<Scene> create(std::string_view name,
                                                    core::UuidGenerator generator);

    // A moved-from scene has a null ID, an empty name and no entities; it
    // may be assigned to or destroyed.
    Scene(Scene&& other) noexcept;
    Scene& operator=(Scene&& other) noexcept;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    ~Scene() = default;

    [[nodiscard]] SceneId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    // Creates an entity with a new ID from the scene's generator.
    [[nodiscard]] core::Result<EntityId> createEntity();
    // Creates an entity with the given ID (loading a document, a later undo).
    // Fails for the null ID and for an ID the scene already contains.
    [[nodiscard]] core::Result<EntityId> createEntity(EntityId entity);

    // Destroys the entity and everything attached to it. Fails for an ID the
    // scene does not contain.
    [[nodiscard]] std::optional<core::Error> destroyEntity(EntityId entity);

    [[nodiscard]] bool contains(EntityId entity) const noexcept;
    // Nothing if the scene contains the entity, otherwise the error that names
    // the scene and the ID (for the null ID too).
    [[nodiscard]] std::optional<core::Error> findEntity(EntityId entity) const;

    // The IDs of all entities in the order they were created (a destroyed
    // entity leaves its place; one created again with its ID goes to the end).
    [[nodiscard]] std::vector<EntityId> entityIds() const;
    [[nodiscard]] std::size_t entityCount() const noexcept { return byId_.size(); }

    // Destroys all entities; the scene keeps its ID and name.
    void clear() noexcept;

private:
    Scene(SceneId sceneId, std::string name, core::UuidGenerator generator) noexcept;

    [[nodiscard]] std::string describe(EntityId entity) const;
    void insert(EntityId entity);

    SceneId id_;
    std::string name_;
    core::UuidGenerator generator_;
    // Each entity's place in the creation order, and the order itself.
    std::map<EntityId, std::uint64_t> byId_;
    std::map<std::uint64_t, EntityId> bySequence_;
    std::uint64_t nextSequence_ = 0;
};

} // namespace engine::scene
