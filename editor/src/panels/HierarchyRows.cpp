#include "editor/panels/HierarchyRows.hpp"

#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ParentComponent.hpp"

#include <map>
#include <set>
#include <utility>

namespace editor {

using engine::scene::EntityId;
using engine::scene::Scene;

std::string entityLabel(const Scene& scene, EntityId entity)
{
    if (auto name = scene.component<engine::scene::NameComponent>(entity)) {
        return std::move(name).value().value;
    }
    return entity.toString();
}

std::vector<HierarchyRow> hierarchyRows(const Scene& scene)
{
    const std::vector<EntityId> entities = scene.entityIds();
    // Each entity's children, in creation order; top-level entities under null.
    std::map<EntityId, std::vector<EntityId>> children;
    std::map<EntityId, EntityId> parentOf;
    for (const EntityId entity : entities) {
        EntityId parent;
        if (const auto link = scene.component<engine::scene::ParentComponent>(entity)) {
            if (scene.contains(link.value().entity)) {
                parent = link.value().entity;
            }
        }
        parentOf[entity] = parent;
        children[parent].push_back(entity);
    }

    std::vector<HierarchyRow> rows;
    rows.reserve(entities.size());
    std::set<EntityId> listed;
    // Depth first without recursion: a deep chain of parents cannot exhaust
    // the stack.
    const auto addTree = [&](EntityId top, std::size_t topDepth) {
        std::vector<std::pair<EntityId, std::size_t>> pending{{top, topDepth}};
        while (!pending.empty()) {
            const auto [entity, depth] = pending.back();
            pending.pop_back();
            if (!listed.insert(entity).second) {
                continue;
            }
            rows.push_back({entity, depth == 0 ? EntityId{} : parentOf[entity], depth,
                            entityLabel(scene, entity)});
            const auto found = children.find(entity);
            if (found == children.end()) {
                continue;
            }
            // Reversed onto the stack, so the first child comes out first.
            for (auto child = found->second.rbegin(); child != found->second.rend(); ++child) {
                pending.emplace_back(*child, depth + 1);
            }
        }
    };
    if (const auto top = children.find(EntityId{}); top != children.end()) {
        for (const EntityId entity : top->second) {
            addTree(entity, 0);
        }
    }
    // Only a scene that broke its own rules leaves entities here.
    for (const EntityId entity : entities) {
        if (!listed.contains(entity)) {
            addTree(entity, 0);
        }
    }
    return rows;
}

bool sameTree(std::span<const HierarchyRow> a, std::span<const HierarchyRow> b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].entity != b[i].entity || a[i].parent != b[i].parent || a[i].depth != b[i].depth) {
            return false;
        }
    }
    return true;
}

} // namespace editor
