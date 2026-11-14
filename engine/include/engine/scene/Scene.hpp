#pragma once

// A scene: the entities of one game world document and their components
// (M4.1, M4.2; ADR 0020, ADR 0021).
//
// Entities are identified only by their persistent EntityId; the scene hands
// out no pointer or reference into its storage, so a destroyed entity cannot
// be reached through stale data: every operation on an ID the scene does not
// contain fails with an error that names the scene and the ID.
//
// Components are plain values. They enter the scene only through addComponent
// and replaceComponent, which validate them first, so every component in a
// scene passes its validate; reading one returns a copy. The storage behind
// this API is EnTT, private to the implementation (ADR 0021).
//
// A scene is a value owned by its caller (no global registry), move-only so
// that entity IDs are not duplicated by accident, and not thread-safe: it is
// used by one thread at a time. It needs no window or GPU.

#include "engine/core/Result.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DirectionalLightComponent.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/SceneId.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
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

// Checks a name of the scene model (a scene's name, an entity's
// NameComponent): not empty, at most kMaxNameBytes bytes, valid UTF-8, no
// control characters (U+0000-U+001F, U+007F-U+009F). The error starts with
// `what`, e.g. "scene name: invalid UTF-8 at byte 3".
[[nodiscard]] std::optional<core::Error> validateName(std::string_view name, std::string_view what);

// The component types a scene stores.
template <typename T>
concept SceneComponent =
    std::same_as<T, NameComponent> || std::same_as<T, TransformComponent> ||
    std::same_as<T, CameraComponent> || std::same_as<T, DirectionalLightComponent>;

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
    ~Scene();

    [[nodiscard]] SceneId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    // Creates an entity, without components, with a new ID from the scene's
    // generator.
    [[nodiscard]] core::Result<EntityId> createEntity();
    // Creates an entity, without components, with the given ID (loading a
    // document, a later undo). Fails for the null ID and for an ID the scene
    // already contains.
    [[nodiscard]] core::Result<EntityId> createEntity(EntityId entity);

    // Destroys the entity together with its components. Fails for an ID the
    // scene does not contain.
    [[nodiscard]] std::optional<core::Error> destroyEntity(EntityId entity);

    [[nodiscard]] bool contains(EntityId entity) const noexcept;
    // Nothing if the scene contains the entity, otherwise the error that names
    // the scene and the ID (for the null ID too).
    [[nodiscard]] std::optional<core::Error> findEntity(EntityId entity) const;

    // The IDs of all entities in the order they were created (a destroyed
    // entity leaves its place; one created again with its ID goes to the end).
    [[nodiscard]] std::vector<EntityId> entityIds() const;
    [[nodiscard]] std::size_t entityCount() const noexcept;

    // Destroys all entities and their components; the scene keeps its ID,
    // name and generator.
    void clear() noexcept;

    // Component operations. Each fails, changing nothing, for an entity the
    // scene does not contain; the error names the scene, the entity and the
    // component type.

    // Adds a component the entity does not have yet (an entity has at most
    // one component of each type; replaceComponent changes it). Fails for an
    // invalid value with the error of its validate. Rotations and light
    // directions are stored normalized.
    template <SceneComponent T>
    [[nodiscard]] std::optional<core::Error> addComponent(EntityId entity, const T& value);
    // Replaces a component the entity has, under the same rules as
    // addComponent.
    template <SceneComponent T>
    [[nodiscard]] std::optional<core::Error> replaceComponent(EntityId entity, const T& value);
    // Removes a component the entity has.
    template <SceneComponent T>
    [[nodiscard]] std::optional<core::Error> removeComponent(EntityId entity);
    // False for an entity the scene does not contain.
    template <SceneComponent T>
    [[nodiscard]] bool hasComponent(EntityId entity) const noexcept;
    // A copy of a component the entity has.
    template <SceneComponent T>
    [[nodiscard]] core::Result<T> component(EntityId entity) const;

    // The entities that have all the given components, in creation order.
    template <SceneComponent First, SceneComponent... Rest>
    [[nodiscard]] std::vector<EntityId> entitiesWith() const
    {
        std::vector<EntityId> found = entitiesWithComponent<First>();
        if constexpr (sizeof...(Rest) > 0) {
            std::erase_if(found,
                          [this](EntityId entity) { return !(hasComponent<Rest>(entity) && ...); });
        }
        return found;
    }

private:
    // The EnTT registry and the ID index, defined in Scene.cpp. Null in a new,
    // cleared or moved-from scene until an entity is created.
    struct Storage;

    Scene(SceneId sceneId, std::string name, core::UuidGenerator generator) noexcept;

    [[nodiscard]] std::string describe(EntityId entity) const;
    [[nodiscard]] Storage& storage();
    void insert(EntityId entity);

    // The entities having a component of type T, in creation order.
    template <SceneComponent T>
    [[nodiscard]] std::vector<EntityId> entitiesWithComponent() const;

    SceneId id_;
    std::string name_;
    core::UuidGenerator generator_;
    std::unique_ptr<Storage> storage_;
};

} // namespace engine::scene
