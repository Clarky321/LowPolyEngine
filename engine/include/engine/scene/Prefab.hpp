#pragma once

// Prefabs: reusable groups of entities (M4.6, ADR 0025).
//
// A prefab holds copies of its entities' plain components and the parent
// links between them, by prefab-local IDs; it is an asset, saved as a prefab
// document (engine/scene/PrefabDocument.hpp). Instantiating it into a scene
// gives every prefab entity a new EntityId, maps the internal links to the
// new IDs and marks each new entity with a PrefabInstanceComponent. Instances
// copy all values: nothing is shared between instances or with the prefab.
// No nested prefabs, variants or overrides in M4.

#include "engine/assets/AssetId.hpp"
#include "engine/core/Result.hpp"
#include "engine/scene/EntityComponents.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/PrefabInstanceComponent.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::scene {

class Scene;

// Most entities in one prefab.
inline constexpr std::size_t kMaxPrefabEntities = 10000;

struct PrefabEntity {
    // At least 1, unique within the prefab.
    PrefabLocalId localId = 0;
    // The local ID of the parent, another entity of the same prefab.
    std::optional<PrefabLocalId> parent;
    EntityComponents components;

    friend bool operator==(const PrefabEntity&, const PrefabEntity&) = default;
};

struct Prefab {
    // The prefab's asset ID; never null.
    assets::AssetId id;
    // A name under the rules of validateName.
    std::string name;
    // In the order they are instantiated; at least one, at most
    // kMaxPrefabEntities.
    std::vector<PrefabEntity> entities;

    friend bool operator==(const Prefab&, const Prefab&) = default;
};

// Everything a prefab must be before it is saved or instantiated: a non-null
// ID, a valid name, 1 to kMaxPrefabEntities entities with unique local IDs of
// at least 1, parents that are other entities of the prefab without a cycle,
// and components that pass their validate. The error names the entity by its
// index, as a document would: "prefab 'House': entities[2].localId: 3 is
// also entities[0]".
[[nodiscard]] std::optional<core::Error> validate(const Prefab& prefab);

// A prefab of the given scene entities, in the given order, with local IDs 1,
// 2, ... Parent links between them become local links; a parent outside the
// set is an error naming both. A PrefabInstanceComponent of an entity is not
// copied (no nested prefabs). Fails for an empty set, an entity listed twice
// or not in the scene, more than kMaxPrefabEntities entities, and a null ID or
// an invalid name.
[[nodiscard]] core::Result<Prefab> createPrefab(const Scene& scene,
                                                std::span<const EntityId> entities,
                                                assets::AssetId id, std::string_view name);

struct PrefabInstance {
    PrefabInstanceId id;
    // The new entities, in the prefab's entity order.
    std::vector<EntityId> entities;
};

// Adds one instance of the prefab to the scene: new entities (IDs from the
// scene's generator), copies of the components, parent links mapped to the
// new entities, and a PrefabInstanceComponent on each. All or nothing: on any
// error the entities created so far are destroyed again and the scene's
// content is as before (the generator has moved on).
[[nodiscard]] core::Result<PrefabInstance> instantiatePrefab(Scene& scene, const Prefab& prefab);

} // namespace engine::scene
