#include "engine/scene/Prefab.hpp"

#include "engine/scene/Scene.hpp"

#include <format>
#include <map>
#include <set>

namespace engine::scene {

namespace {

// "prefab 'House'", or "prefab" while the name is not valid.
std::string labelOf(const Prefab& prefab)
{
    return validateName(prefab.name, "name") ? std::string("prefab")
                                             : std::format("prefab '{}'", prefab.name);
}

std::string parentPath(std::size_t index)
{
    return std::format("entities[{}].components.parent.entity", index);
}

} // namespace

std::optional<core::Error> validate(const Prefab& prefab)
{
    const std::string label = labelOf(prefab);
    if (prefab.id.isNull()) {
        return core::Error{label + ": id: the null asset ID is not an identity"};
    }
    if (auto error = validateName(prefab.name, "name")) {
        return core::Error{"prefab: " + error->message};
    }
    if (prefab.entities.empty()) {
        return core::Error{label + ": entities: a prefab needs at least one entity"};
    }
    if (prefab.entities.size() > kMaxPrefabEntities) {
        return core::Error{std::format("{}: entities: {} entries, at most {} allowed", label,
                                       prefab.entities.size(), kMaxPrefabEntities)};
    }

    std::map<PrefabLocalId, std::size_t> indices;
    for (std::size_t i = 0; i < prefab.entities.size(); ++i) {
        const PrefabEntity& entity = prefab.entities[i];
        if (entity.localId < 1) {
            return core::Error{std::format("{}: entities[{}].localId: {}, must be at least 1",
                                           label, i, entity.localId)};
        }
        if (const auto [earlier, added] = indices.emplace(entity.localId, i); !added) {
            return core::Error{std::format("{}: entities[{}].localId: {} is also entities[{}]",
                                           label, i, entity.localId, earlier->second)};
        }
        if (auto error = validate(entity.components)) {
            return core::Error{std::format("{}: entities[{}]: {}", label, i, error->message)};
        }
    }

    for (std::size_t i = 0; i < prefab.entities.size(); ++i) {
        const PrefabEntity& entity = prefab.entities[i];
        if (!entity.parent) {
            continue;
        }
        const PrefabLocalId parent = *entity.parent;
        if (parent == entity.localId) {
            return core::Error{
                std::format("{}: {}: an entity cannot be its own parent", label, parentPath(i))};
        }
        const auto found = indices.find(parent);
        if (found == indices.end()) {
            return core::Error{std::format("{}: {}: {} is not the local ID of an entity of the "
                                           "prefab",
                                           label, parentPath(i), parent)};
        }
        // Walk up; reaching this entity again closes a cycle.
        std::size_t current = found->second;
        for (std::size_t step = 0; step <= prefab.entities.size(); ++step) {
            if (current == i) {
                return core::Error{std::format("{}: {}: entity {} is a descendant of this entity, "
                                               "which makes a cycle",
                                               label, parentPath(i), parent)};
            }
            const auto& next = prefab.entities[current].parent;
            if (!next) {
                break;
            }
            const auto nextFound = indices.find(*next);
            if (nextFound == indices.end()) {
                break; // Reported when that entity's own link is checked.
            }
            current = nextFound->second;
        }
    }
    return std::nullopt;
}

core::Result<Prefab> createPrefab(const Scene& scene, std::span<const EntityId> entities,
                                  assets::AssetId id, std::string_view name)
{
    Prefab prefab;
    prefab.id = id;
    prefab.name = std::string(name);
    const std::string label = labelOf(prefab);
    if (entities.empty()) {
        return core::Error{label + ": no entities given"};
    }
    if (entities.size() > kMaxPrefabEntities) {
        return core::Error{std::format("{}: {} entities, at most {} allowed", label,
                                       entities.size(), kMaxPrefabEntities)};
    }

    std::map<EntityId, PrefabLocalId> localIds;
    for (const EntityId entity : entities) {
        if (auto error = scene.findEntity(entity)) {
            return core::Error{label + ": " + error->message};
        }
        const auto localId = static_cast<PrefabLocalId>(localIds.size() + 1);
        if (!localIds.emplace(entity, localId).second) {
            return core::Error{
                std::format("{}: entity {} is listed twice", label, entity.toString())};
        }
    }

    prefab.entities.reserve(entities.size());
    for (const EntityId entity : entities) {
        PrefabEntity copy;
        copy.localId = localIds.at(entity);
        copy.components = componentsOf(scene, entity);
        if (const auto parent = scene.component<ParentComponent>(entity)) {
            const auto found = localIds.find(parent.value().entity);
            if (found == localIds.end()) {
                return core::Error{std::format("{}: entity {} has the parent {}, which is not one "
                                               "of the prefab's entities",
                                               label, entity.toString(),
                                               parent.value().entity.toString())};
            }
            copy.parent = found->second;
        }
        prefab.entities.push_back(std::move(copy));
    }
    if (auto error = validate(prefab)) {
        return *error;
    }
    return prefab;
}

core::Result<PrefabInstance> instantiatePrefab(Scene& scene, const Prefab& prefab)
{
    if (auto error = validate(prefab)) {
        return *error;
    }
    const std::string label = labelOf(prefab);

    // An instance ID the scene does not use yet.
    std::set<PrefabInstanceId> used;
    for (const EntityId entity : scene.entitiesWith<PrefabInstanceComponent>()) {
        used.insert(scene.component<PrefabInstanceComponent>(entity).value().instance);
    }
    PrefabInstanceId instance;
    for (int attempt = 0; attempt < kMaxIdAttempts && instance.isNull(); ++attempt) {
        const PrefabInstanceId candidate{scene.generateId()};
        if (!used.contains(candidate)) {
            instance = candidate;
        }
    }
    if (instance.isNull()) {
        return core::Error{std::format("{}: no unused prefab instance ID after {} attempts", label,
                                       kMaxIdAttempts)};
    }

    PrefabInstance result{instance, {}};
    const auto rollBack = [&](const core::Error& error) {
        for (const EntityId entity : result.entities) {
            (void)scene.destroyEntity(entity);
        }
        return core::Error{std::format("{}: {}", label, error.message)};
    };

    // Every entity first, so that parent links find their parents.
    std::map<PrefabLocalId, EntityId> created;
    for (const PrefabEntity& entity : prefab.entities) {
        auto made = scene.createEntity();
        if (!made) {
            return rollBack(made.error());
        }
        result.entities.push_back(made.value());
        created.emplace(entity.localId, made.value());
    }
    for (std::size_t i = 0; i < prefab.entities.size(); ++i) {
        const PrefabEntity& entity = prefab.entities[i];
        const EntityId target = result.entities[i];
        std::optional<core::Error> error = addComponents(scene, target, entity.components);
        if (!error && entity.parent) {
            error = scene.addComponent(target, ParentComponent{created.at(*entity.parent)});
        }
        if (!error) {
            error = scene.addComponent(
                target, PrefabInstanceComponent{prefab.id, instance, entity.localId});
        }
        if (error) {
            return rollBack(*error);
        }
    }
    return result;
}

} // namespace engine::scene
